#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/klock.h>

#include <sel4/sel4.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>


static klock_t klog_public_lock   = KLOCK_INITIALIZER;
static klock_t klog_internal_lock = KLOCK_INITIALIZER;

static struct klog_sink klog_sinks[KLOG_MAX_SINKS];
static char klog_early_buf[KLOG_EARLY_BUFSIZE];

static size_t klog_early_buf_used = 0;
static bool klog_early_truncated = false;

static klog_store_fn klog_cur_store_fn;

static void klog_early_store_fn(const char *data, size_t len);
static void klog_dummy_sink(const char *data, size_t len);
static void klog_sink_sel4(const char *data, size_t len);


struct klog_emit_ctx {
	char buf[128];
	size_t used;
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


void
klog_init_early(void)
{
	/* Set up our locks. */
	klock_init(&klog_public_lock);
	klock_init(&klog_internal_lock);

	/* Set up our sinks. */
	for (size_t i = 0; i < KLOG_MAX_SINKS; i++) {
		klog_sinks[i].write   = &klog_dummy_sink;
		klog_sinks[i].enabled = false;
	}

	/* Add the first sink. */
	klog_add_sink(&klog_sink_sel4);

	/* Configure the initial log store. */
	klog_cur_store_fn = &klog_early_store_fn;
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


static void
klog_dummy_sink(const char *data, size_t len)
{
	(void)data;
	(void)len;
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


int
klog_add_sink(klog_sink_fn write)
{
	if (write == NULL)
		return -1;

	klog_lock_internal();

	for (size_t i = 0; i < KLOG_MAX_SINKS; i++) {
		if (!klog_sinks[i].enabled &&
		    klog_sinks[i].write == &klog_dummy_sink) {

			klog_sinks[i].write = write;
			klog_sinks[i].enabled = true;

			klog_unlock_internal();
			return 0;
		}
	}

	klog_unlock_internal();
	return -1;
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
klog_sink_sel4(const char *data, size_t len)
{
	/*
	 * seL4_DebugPutString() requires a NUL-terminated string,
	 * while our internal sink interface is length-based.
	 */
	char tmp[128];

	while (len) {
		size_t n = len;

		if (n >= sizeof(tmp))
			n = sizeof(tmp) - 1;

		for (size_t i = 0; i < n; i++)
			tmp[i] = data[i];

		tmp[n] = '\0';

		seL4_DebugPutString(tmp);

		data += n;
		len  -= n;
	}
}


static void
klog_emit_flush(struct klog_emit_ctx *ctx)
{
	if (ctx->used == 0)
		return;

	if (klog_cur_store_fn != NULL)
		klog_cur_store_fn(ctx->buf, ctx->used);

	for (size_t i = 0; i < KLOG_MAX_SINKS; i++) {
		if (klog_sinks[i].enabled)
			klog_sinks[i].write(ctx->buf, ctx->used);
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
	struct klog_emit_ctx ctx = { .used = 0 };
	va_list ap;

	(void)level;

	if (fmt == NULL)
		return;

	klog_lock_internal();

	va_start(ap, fmt);
	klog_vprintf(&ctx, fmt, ap);
	va_end(ap);

	klog_emit_flush(&ctx);

	klog_unlock_internal();
}
