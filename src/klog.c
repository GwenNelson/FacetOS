#include <klog.h>
#include <klock.h>

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

static inline void klog_lock_internal(void);
static inline void klog_unlock_internal(void);

void klog_init_early() {
	// setup our locks
	klock_init(&klog_public_lock);
	klock_init(&klog_internal_lock);

	// setup our sinks
	for(int i=0; i<KLOG_MAX_SINKS; i++) {
		klog_sinks[i].write   = &klog_dummy_sink;
		klog_sinks[i].enabled = false;
	}

	// add the first sink
	klog_add_sink(&klog_sink_sel4);

	// configure klog_cur_store_fn appropriately
	klog_cur_store_fn = &klog_early_store_fn;
}

static void klog_early_store_fn(const char* data, size_t len) {
       size_t available = sizeof(klog_early_buf) - klog_early_buf_used;
       if (len > available) {
          len = available;
          klog_early_truncated = true;
       }
       for (size_t i = 0; i < len; i++) {
           klog_early_buf[klog_early_buf_used + i] = data[i];
       }

       klog_early_buf_used += len;
}

static void klog_dummy_sink(const char* data, size_t len) {
	(void)data;
	(void)len;
}

static inline void klog_lock_internal() {
	klock_lock(&klog_internal_lock);
}

static inline void klog_unlock_internal() {
	klock_unlock(&klog_internal_lock);
}

int klog_add_sink(klog_sink_fn write) {
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

void klog_lock() {
	klock_lock(&klog_public_lock);
}

void klog_unlock() {
	klock_unlock(&klog_public_lock);
}

static void klog_sink_sel4(const char *data, size_t len)
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

void klog(enum log_level level, const char* fmt, ...) {
}
