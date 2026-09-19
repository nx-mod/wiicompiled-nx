// Raises the default stack for every thread created through pthread_create on
// Switch, via the linker's --wrap=pthread_create (see dawn/CMakeLists.txt).
//
// std::thread passes a null attribute, and libnx then falls back to a 128 KiB
// stack. Aurora's shader-compile workers run Tint there, and Tint's uniformity
// analysis recurses deeply enough on real MKW shaders to run straight off the
// end of it: the first crash on hardware had SP 33 KiB below the bottom of a
// 132 KiB stack region, inside tint::resolver::AnalyzeUniformity. Chromium runs
// Tint on multi-megabyte stacks for the same reason.
//
// Wrapping the one entry point covers every thread in the process - Aurora,
// Dawn, Tint and libstdc++ - rather than patching each spawner and missing one.
#if defined(__SWITCH__)

#include <pthread.h>

#include <cstddef>

extern "C" {
int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*),
                          void* arg);

// Callers that ask for a specific stack are left alone; only the unspecified
// default is raised. Stacks come from the heap, which has ample room: ~26
// worker threads at this size is roughly 100 MiB.
constexpr std::size_t kDefaultThreadStackSize = 4 * 1024 * 1024;

int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*),
                          void* arg) {
    if (attr != nullptr) {
        std::size_t requested = 0;
        if (pthread_attr_getstacksize(attr, &requested) == 0 && requested >= kDefaultThreadStackSize) {
            return __real_pthread_create(thread, attr, start, arg);
        }
    }

    pthread_attr_t sized;
    if (attr != nullptr) {
        sized = *attr;
    } else if (pthread_attr_init(&sized) != 0) {
        return __real_pthread_create(thread, attr, start, arg);
    }
    pthread_attr_setstacksize(&sized, kDefaultThreadStackSize);
    const int result = __real_pthread_create(thread, &sized, start, arg);
    if (attr == nullptr) {
        pthread_attr_destroy(&sized);
    }
    return result;
}
}

#endif // __SWITCH__
