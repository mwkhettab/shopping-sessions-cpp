#include <cstddef>
#include <new>
extern "C" void* library_allocate(std::size_t bytes,std::size_t alignment) {
    return ::operator new[](bytes,std::align_val_t(alignment));
}
extern "C" void library_release(void *p,std::size_t alignment) {
    ::operator delete[](p,std::align_val_t(alignment));
}
