#include <Arduino.h>
#include "esp32-hal-psram.h"

static bool _psramAvailable = false;

__attribute__((constructor(101)))
static void initPSRAM() {
    _psramAvailable = psramInit();
}

void* operator new(size_t size) {
    void* ptr = nullptr;
    if (_psramAvailable) {
        ptr = ps_malloc(size);
    }
    if (!ptr) {
        ptr = malloc(size);
    }
    if (!ptr) {
        ets_printf("alloc failed: %u bytes\n", size);
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}
