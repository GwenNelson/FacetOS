#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/klock.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static klock_t klog_public_lock   = KLOCK_INITIALIZER;
static klock_t klog_internal_lock = KLOCK_INITIALIZER;

typedef void (*klog_store_fn)(const char *data, size_t len);

typedef struct KlogRoute {
	ILoggingSink *sink;
	LogLevel maximum_level;
	bool enabled;
} KlogRoute;

static KlogRoute klog_routes[KLOG_MAX_SINKS];
static ILoggingSink *klog_emergency_sink;
static char klog_early_buf[KLOG_EARLY_BUFSIZE];

static char  *klog_prod_buf = NULL;
static size_t klog_prod_buf_used = 0;
static size_t klog_prod_buf_size = 0;

static size_t klog_early_buf_used = 0;
static bool klog_early_truncated = false;
static unsigned char klog_in_progress = 0;

static klog_store_fn klog_cur_store_fn;

static void klog_early_store_fn(const char *data, size_t len);


struct klog_emit_ctx {
	char buf[128];
	size_t used;
	LogLevel level;
};


static void klog_emit_flush(struct klog_emit_ctx *ctx);
static void klog_emit_char(struct klog_emit_ctx *ctx, char c);
static void klog_emit_string(struct klog_emit_ctx *ctx, const char *str);
static void klog_emit_uint(struct klog_emit_ctx *ctx,
			   uint64_t value,
			   unsigned int base);
static void klog_emit_int(struct klog_emit_ctx *ctx, int value);
static void klog_emit_int64(struct klog_emit_ctx *ctx, int64_t value);
static void klog_vprintf(struct klog_emit_ctx *ctx,
			 const char *fmt,
			 va_list ap);

static inline void klog_lock_internal(void);
static inline void klog_unlock_internal(void);


static FacetResult
klog_emit_to_sink(ILoggingSink *sink, const char *data, size_t len)
{
	if (sink == NULL || sink->emit == NULL)
		return FACET_INVALID_ARGUMENT;
	FacetString message = { .data = data, .length = len };
	return sink->emit(sink->self, &message);
}


static void
klog_emergency_write(const char *data, size_t len)
{
	if (klog_emergency_sink != NULL)
		(void)klog_emit_to_sink(klog_emergency_sink, data, len);
}


void klog_init_early(ILoggingSink *emergency_sink)
{
	/* Set up our locks. */
	klock_init(&klog_public_lock);
	klock_init(&klog_internal_lock);

	memset(klog_routes, 0, sizeof(klog_routes));
	klog_emergency_sink = emergency_sink;
	if (emergency_sink != NULL && emergency_sink->emit != NULL) {
		klog_routes[0].sink = emergency_sink;
		klog_routes[0].maximum_level = LogLevel_Trace;
		klog_routes[0].enabled = true;
	}

	/* Configure the initial log store. */
	klog_early_buf_used = 0;
	klog_early_truncated = false;
	klog_prod_buf = NULL;
	klog_prod_buf_used = 0;
	klog_prod_buf_size = 0;
	klog_in_progress = 0;
	klog_cur_store_fn = &klog_early_store_fn;
}

static void klog_kpanic_msg(const char *msg) {
	klog_emergency_write(msg, strlen(msg));
}

static void klog_kpanic(const char *msg) {
	klog_kpanic_msg("\n\n\nKERNEL PANIC: ");
	klog_kpanic_msg(msg);
	klog_kpanic_msg("\n\n\n");
	for(;;) kpanic_halt();
}

static void klog_prod_store_fn(const char* data, size_t len) {
       size_t available = klog_prod_buf_size - klog_prod_buf_used;
       if (len > available) {
	  /* The allocator emits debug logs while its lock is held.  Growing the
	   * log buffer here would recursively enter kmalloc and deadlock.  The
	   * configured sinks still receive this message below. */
	  if (kmalloc_is_in_progress())
	     return;
	  if (len > SIZE_MAX - klog_prod_buf_used - 1)
	     klog_kpanic("klog buffer size overflow!");
	  size_t new_size = klog_prod_buf_used + len + 1;
	  void* new_buf = krealloc(klog_prod_buf, new_size);
	  if(new_buf==NULL) klog_kpanic("Could not expand klog buffer!"); // TODO - at some point make it use a ringbuffer instead
          klog_prod_buf = new_buf;
	  klog_prod_buf_size = new_size;
       }

	for (size_t i = 0; i < len; i++)
		klog_prod_buf[klog_prod_buf_used + i] = data[i];

	klog_prod_buf_used += len;
	klog_prod_buf[klog_prod_buf_used] = '\0';
}


static bool
klog_string_equal(FacetString left, FacetString right)
{
	if ((left.length != 0 && left.data == NULL) ||
	    (right.length != 0 && right.data == NULL))
		return false;
	return left.length == right.length &&
	       (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}


static bool
klog_valid_maximum_level(LogLevel level)
{
	return level == LogLevel_None || level == LogLevel_Fatal ||
	       level == LogLevel_Error || level == LogLevel_Warning ||
	       level == LogLevel_Info || level == LogLevel_Debug ||
	       level == LogLevel_Trace;
}


int klog_init_postboot(ILoggingConfig *config,
			const KlogSinkBinding *bindings,
			size_t binding_count)
{
	if (config == NULL || config->getsinks == NULL ||
	    (bindings == NULL && binding_count != 0))
		return -1;

	FacetArray_Sink configured = {0};
	if (config->getsinks(config->self, &configured) != FACET_OK ||
	    configured.count > KLOG_MAX_SINKS ||
	    (configured.data == NULL && configured.count != 0))
		return -1;

	KlogRoute routes[KLOG_MAX_SINKS] = {0};
	for (size_t i = 0; i < configured.count; i++) {
		if (!klog_valid_maximum_level(configured.data[i].level))
			return -1;
		size_t match = binding_count;
		for (size_t j = 0; j < binding_count; j++) {
			if (!klog_string_equal(configured.data[i].name,
			                       bindings[j].name))
				continue;
			if (match != binding_count)
				return -1;
			match = j;
		}
		if (match == binding_count)
			return -1;
		if (bindings[match].sink == NULL ||
		    configured.data[i].level == LogLevel_None)
			continue;
		routes[i].sink = bindings[match].sink;
		routes[i].maximum_level = configured.data[i].level;
		routes[i].enabled = true;
	}

	char *new_buffer = (char*)kmalloc(klog_early_buf_used + 1);
	if (new_buffer == NULL)
		return -1;

	klog_lock();
	klock_lock(&klog_internal_lock);
	memcpy(new_buffer, klog_early_buf, klog_early_buf_used);
	new_buffer[klog_early_buf_used] = '\0';
	klog_prod_buf = new_buffer;
	klog_prod_buf_used = klog_early_buf_used;
	klog_prod_buf_size = klog_early_buf_used + 1;
	memcpy(klog_routes, routes, sizeof(klog_routes));
	klog_cur_store_fn = klog_prod_store_fn;

	klock_unlock(&klog_internal_lock);
	if(klog_early_truncated) klog(LOG_INFO,"...\n(early logbuf truncated)\n");
	klog(LOG_INFO,"Switched to dynamic log buffer!\n");
	klog_unlock();
	return 0;
}

static void
klog_early_store_fn(const char *data, size_t len)
{
	size_t available = sizeof(klog_early_buf) - klog_early_buf_used;

	if (len > available) {
		len = available;
		klog_early_truncated = true;
	}

	for (size_t i = 0; i < len; i++)
		klog_early_buf[klog_early_buf_used + i] = data[i];

	klog_early_buf_used += len;
}

static inline void
klog_lock_internal(void)
{
	klock_lock(&klog_internal_lock);
}


static inline void
klog_unlock_internal(void)
{
	klock_unlock(&klog_internal_lock);
}


void
klog_lock(void)
{
	klock_lock(&klog_public_lock);
}


void
klog_unlock(void)
{
	klock_unlock(&klog_public_lock);
}


static void
klog_emit_flush(struct klog_emit_ctx *ctx)
{
	if (ctx->used == 0)
		return;

	if (klog_cur_store_fn != NULL)
		klog_cur_store_fn(ctx->buf, ctx->used);

	for (size_t i = 0; i < KLOG_MAX_SINKS; i++) {
		KlogRoute *route = &klog_routes[i];
		if (!route->enabled || ctx->level > route->maximum_level)
			continue;
		if (klog_emit_to_sink(route->sink, ctx->buf, ctx->used) != FACET_OK) {
			route->enabled = false;
			static const char failure[] =
				"klog: configured sink failed; disabling route\n";
			klog_emergency_write(failure, sizeof(failure) - 1);
			if (route->sink != klog_emergency_sink)
				klog_emergency_write(ctx->buf, ctx->used);
		}
	}

	ctx->used = 0;
}


static void
klog_emit_char(struct klog_emit_ctx *ctx, char c)
{
	ctx->buf[ctx->used++] = c;

	if (ctx->used == sizeof(ctx->buf))
		klog_emit_flush(ctx);
}


static void
klog_emit_string(struct klog_emit_ctx *ctx, const char *str)
{
	if (str == NULL)
		str = "(null)";

	while (*str != '\0')
		klog_emit_char(ctx, *str++);
}


static void
klog_emit_uint(struct klog_emit_ctx *ctx,
		uint64_t value,
		unsigned int base)
{
	static const char digits[] = "0123456789abcdef";
	char buf[sizeof(uint64_t) * 8];
	size_t used = 0;

	if (value == 0) {
		klog_emit_char(ctx, '0');
		return;
	}

	while (value != 0) {
		buf[used++] = digits[value % base];
		value /= base;
	}

	while (used != 0)
		klog_emit_char(ctx, buf[--used]);
}


static void
klog_emit_int(struct klog_emit_ctx *ctx, int value)
{
	unsigned int magnitude;

	if (value < 0) {
		klog_emit_char(ctx, '-');

		/*
		 * Avoid overflowing when value is INT_MIN.
		 */
		magnitude = (unsigned int)(-(value + 1)) + 1;
	} else {
		magnitude = (unsigned int)value;
	}

	klog_emit_uint(ctx, magnitude, 10);
}


static void
klog_emit_int64(struct klog_emit_ctx *ctx, int64_t value)
{
	uint64_t magnitude;

	if (value < 0) {
		klog_emit_char(ctx, '-');

		/*
		 * Avoid overflowing when value is INT64_MIN.
		 */
		magnitude = (uint64_t)(-(value + 1)) + 1;
	} else {
		magnitude = (uint64_t)value;
	}

	klog_emit_uint(ctx, magnitude, 10);
}


static void
klog_vprintf(struct klog_emit_ctx *ctx, const char *fmt, va_list ap)
{
	while (*fmt != '\0') {
		if (*fmt != '%') {
			klog_emit_char(ctx, *fmt++);
			continue;
		}

		fmt++;

		switch (*fmt) {
		case '%':
			klog_emit_char(ctx, '%');
			break;

		case 's':
			klog_emit_string(ctx,
					 va_arg(ap, const char *));
			break;

		case 'd':
		case 'i':
			klog_emit_int(ctx,
				      va_arg(ap, int));
			break;

		case 'u':
			klog_emit_uint(ctx,
				       va_arg(ap, unsigned int),
				       10);
			break;

		case 'x':
			klog_emit_uint(ctx,
				       va_arg(ap, unsigned int),
				       16);
			break;

		case 'z':
			fmt++;

			switch (*fmt) {
			case 'u':
				klog_emit_uint(ctx,
					       (uint64_t)va_arg(ap, size_t),
					       10);
				break;

			case 'x':
				klog_emit_uint(ctx,
					       (uint64_t)va_arg(ap, size_t),
					       16);
				break;

			case '\0':
				klog_emit_string(ctx, "%z");
				return;

			default:
				klog_emit_string(ctx, "%z");
				klog_emit_char(ctx, *fmt);
				break;
			}

			break;

		case 'l':
			fmt++;

			if (*fmt == 'l') {
				fmt++;

				switch (*fmt) {
				case 'd':
				case 'i':
					klog_emit_int64(
						ctx,
						(int64_t)va_arg(ap, long long));
					break;

				case 'u':
					klog_emit_uint(
						ctx,
						(uint64_t)va_arg(
							ap,
							unsigned long long),
						10);
					break;

				case 'x':
					klog_emit_uint(
						ctx,
						(uint64_t)va_arg(
							ap,
							unsigned long long),
						16);
					break;

				case '\0':
					klog_emit_string(ctx, "%ll");
					return;

				default:
					klog_emit_string(ctx, "%ll");
					klog_emit_char(ctx, *fmt);
					break;
				}
			} else {
				switch (*fmt) {
				case 'd':
				case 'i':
					klog_emit_int64(
						ctx,
						(int64_t)va_arg(ap, long));
					break;

				case 'u':
					klog_emit_uint(
						ctx,
						(uint64_t)va_arg(
							ap,
							unsigned long),
						10);
					break;

				case 'x':
					klog_emit_uint(
						ctx,
						(uint64_t)va_arg(
							ap,
							unsigned long),
						16);
					break;

				case '\0':
					klog_emit_string(ctx, "%l");
					return;

				default:
					klog_emit_string(ctx, "%l");
					klog_emit_char(ctx, *fmt);
					break;
				}
			}

			break;

		case 'p':
			klog_emit_string(ctx, "0x");
			klog_emit_uint(
				ctx,
				(uint64_t)(uintptr_t)va_arg(ap, void *),
				16);
			break;

		case '\0':
			klog_emit_char(ctx, '%');
			return;

		default:
			/*
			 * Preserve unknown format specifiers literally.
			 */
			klog_emit_char(ctx, '%');
			klog_emit_char(ctx, *fmt);
			break;
		}

		fmt++;
	}
}


void
klog_dump_debug(void)
{
	klog_lock();

	klog(LOG_DEBUG, "FacetOS klog debug dump:\n");

	klog(LOG_DEBUG,
	     "Test parsing: below should show "
	     "\"Hello FacetOS: d=-123 u=456 x=deadbeef %%\"\n");

	klog(LOG_DEBUG,
	     "Hello %s: d=%d u=%u x=%x %%\n",
	     "FacetOS",
	     -123,
	     456U,
	     0xdeadbeefU);

	klog(LOG_DEBUG,
	     "64-bit: u=%llu x=%llx d=%lld\n",
	     0x123456789abcdef0ULL,
	     0x123456789abcdef0ULL,
	     -123456789012345LL);

	klog(LOG_DEBUG,
	     "size_t: u=%zu x=%zx\n",
	     (size_t)0x12345678,
	     (size_t)0x12345678);

	klog(LOG_DEBUG,
	     "Early logbuf used: %zu bytes\n",
	     klog_early_buf_used);

	klog(LOG_DEBUG,
	     "Early logbuf @ %p\n",
	     (void *)klog_early_buf);

	klog_unlock();
}


void
klog(enum log_level level, const char *fmt, ...)
{
	LogLevel configured_level;
	switch (level) {
	case LOG_DEBUG: configured_level = LogLevel_Debug; break;
	case LOG_INFO: configured_level = LogLevel_Info; break;
	case LOG_WARN: configured_level = LogLevel_Warning; break;
	case LOG_ERROR: configured_level = LogLevel_Error; break;
	default: return;
	}
	struct klog_emit_ctx ctx = {
		.used = 0,
		.level = configured_level,
	};
	va_list ap;

	if (fmt == NULL)
		return;

	/* Allocator activity can emit debug logs while a log is being formatted
	 * or stored.  Drop nested messages instead of reacquiring this lock. */
	if (__atomic_exchange_n(&klog_in_progress, 1, __ATOMIC_ACQUIRE))
		return;

	klog_lock_internal();

	va_start(ap, fmt);
	klog_vprintf(&ctx, fmt, ap);
	va_end(ap);

	klog_emit_flush(&ctx);

	klog_unlock_internal();
	__atomic_store_n(&klog_in_progress, 0, __ATOMIC_RELEASE);
}
