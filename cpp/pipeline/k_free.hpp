#pragma once
#include "k_ref.hpp"

namespace pipeline::k_free {
using Bytes = k_ref::Bytes;

namespace detail {
template<class Batch> struct Sink {
    Batch *batch;
    void *context;
    void (*flush)(void *);
    void ev_flush() { flush(context); }
    void ck_flush() { flush(context); }
    Batch *ev() { return batch; }
    Batch *ck() { return batch; }
};
bool available();
void events_x86(Bytes, Sink<EvBatch>, bool chain);
void clicks_x86(Bytes, Sink<CkBatch>, bool chain);

template<bool Chain> struct Kernel {
    static constexpr const char *NAME=Chain?"freen":"free";

    template<class S> static void events(Bytes buf,S &sink) {
#if defined(__x86_64__)
        if (available()) events_x86(buf,{sink.ev(),&sink,[](void *p){static_cast<S *>(p)->ev_flush();}},Chain);
        else
#endif
            k_ref::Ref::events(buf,sink);
        sink.ev_flush();
    }
    template<class S> static void clicks(Bytes buf,S &sink) {
#if defined(__x86_64__)
        if (available()) clicks_x86(buf,{sink.ck(),&sink,[](void *p){static_cast<S *>(p)->ck_flush();}},Chain);
        else
#endif
            k_ref::Ref::clicks(buf,sink);
        sink.ck_flush();
    }
};
}
using Free=detail::Kernel<false>;
using FreeN=detail::Kernel<true>;
}
