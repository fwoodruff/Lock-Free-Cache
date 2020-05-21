//
// I have taken Anthony Williams' implementation of an atomic shared pointer
// https://bitbucket.org/anthonyw/atomic_shared_ptr
// I have removed features not used in this project (aliasing pointers, weak pointers etc.)
// I have then added a bool 'mark' member to the pointer types following the work here:
// https://timharris.uk/papers/2001-disc.pdf
// I have then updated the code for C++17:
// removing std::auto_ptr constructors and laundering memory

#ifndef harris_ptr_hpp
#define harris_ptr_hpp

#include <atomic>
#include <memory>


namespace frd {
    template<class T> class shared_ptr;
    
    
    template<typename T>
    struct shared_ptr_header{
        struct counter{
            long external_counters;
            long count;
            
            counter() noexcept:
            external_counters(0),
            count(0)
            {}
        };
        std::aligned_storage_t<sizeof(T),alignof(T)> storage;
        std::atomic<counter> count;
        
        T* get_ptr() {
            return std::launder(reinterpret_cast<T*>(&storage));
        }
        template<typename ... Args>
        shared_ptr_header(Args&& ... args) : count(counter()) {
            new(&storage) T(static_cast<Args&&>(args)...);
        }
        
        void modify_count(long internal, long external) {
            counter old=count.load(std::memory_order_relaxed);
            counter new_count;
            for(;;){
                new_count=old;
                new_count.count += internal;
                new_count.external_counters += external;
                if(count.compare_exchange_weak(old,new_count))
                    break;
            }
            if(!new_count.count && !new_count.external_counters){
                get_ptr()->~T();
                delete this;
            }
        }
    };
    
    template<typename T,typename ... Args>
    shared_ptr<T> make_shared(Args&& ... args);
    
    template<class T> class shared_ptr {
    private:
        
        shared_ptr_header<T>* header;
        bool mark;
        
        template<typename U>
        friend class atomic_shared_ptr;
        template<typename U>
        friend class shared_ptr;
        
        template<typename U,typename ... Args>
        friend shared_ptr<U> make_shared(Args&& ... args);
        
        
        shared_ptr(shared_ptr_header<T>* header_):
        header(header_), mark(false) {
            if(header) header->modify_count(1,0);
        }
        
        void clear() {
            mark = false;
            header=nullptr;
        }
        
    public:
        typedef T element_type;
        
        constexpr shared_ptr() noexcept:
        header(nullptr), mark(false)
        {}
        shared_ptr(const shared_ptr& r) noexcept:
        header(r.header), mark(r.mark) {
            if(header) header->modify_count(1,0);
        }
        shared_ptr(shared_ptr&& r) noexcept:
        header(r.header), mark(r.mark)
        {
            r.clear();
        }
        constexpr shared_ptr(std::nullptr_t) : shared_ptr() { }
        
        ~shared_ptr() { if(header) header->modify_count(-1,0); }
        
        shared_ptr& operator=(const shared_ptr& r) noexcept
        {
            if(&r!=this) {
                shared_ptr temp(r);
                swap(temp);
            }
            return *this;
        }
        shared_ptr& operator=(shared_ptr&& r) noexcept {
            swap(r);
            r.reset();
            return *this;
        }
        void swap(shared_ptr& r) noexcept {
            std::swap(mark,r.mark);
            std::swap(header,r.header);
        }
        void reset() noexcept {
            if(header) header->modify_count(-1,0);
            clear();
        }
        T* get() const noexcept { return header->get_ptr(); }
        T& operator*() const noexcept {  return *get(); }
        T* operator->() const noexcept { return get(); }
        explicit operator bool() const noexcept {  return get(); }
        bool get_mark() const noexcept { return mark; }
        void set_mark(bool val) const noexcept { mark = val; }
        
        
        friend inline bool operator==(shared_ptr const& lhs,shared_ptr const& rhs) {
            return lhs.get()==rhs.get() and lhs.mark == rhs.mark;
        }
        
        friend inline bool operator!=(shared_ptr const& lhs,shared_ptr const& rhs) {
            return !(lhs==rhs);
        }
    };
    
    template<typename T,typename ... Args>
    shared_ptr<T> make_shared(Args&& ... args){
        return shared_ptr<T>( new shared_ptr_header<T>( static_cast<Args&&>(args)...));
    }
    
    template <class T>
    class atomic_shared_ptr
    {
        template<typename U>
        friend class atomic_shared_ptr;
        
        struct counted_ptr{
            long access_count: CHAR_BIT * sizeof(void*) - 1;
            bool mark: 1;
            shared_ptr_header<T>* ptr;
            
            counted_ptr() noexcept:
            access_count(0),ptr(nullptr),mark(false)
            {}
            
            counted_ptr(shared_ptr_header<T>* ptr_, bool mark_):
            access_count(0),ptr(ptr_),mark(mark_)
            {}
            
            counted_ptr(shared_ptr_header<T>* ptr_, bool mark_, long access_count_):
            access_count(access_count_),ptr(ptr_),mark(mark_)
            {}
        };
        
        alignas(sizeof(counted_ptr)) mutable std::atomic<counted_ptr> p;
        
        
        struct local_access{
            std::atomic<counted_ptr>& p;
            counted_ptr val;
            
            void acquire(std::memory_order order){
                if(!val.ptr)
                    return;
                for(;;){
                    counted_ptr newval=val;
                    ++newval.access_count;
                    if(p.compare_exchange_weak(val,newval,order))
                        break;
                }
                ++val.access_count;
            }
            
            local_access(
                         std::atomic<counted_ptr>& p_,
                         std::memory_order order=std::memory_order_relaxed):
            p(p_),val(p.load(order))
            {
                acquire(order);
            }
            
            ~local_access()
            {
                release();
            }
            
            void release(){
                if(!val.ptr)
                    return;
                counted_ptr target=val;
                do {
                    counted_ptr newval=target;
                    --newval.access_count;
                    if(p.compare_exchange_weak(target,newval))
                        break;
                } while(target.ptr==val.ptr);
                if(target.ptr!=val.ptr){
                    val.ptr->modify_count(0,-1);
                }
            }
            
            void refresh(counted_ptr newval,std::memory_order order){
                if(newval.ptr==val.ptr)
                    return;
                release();
                val=newval;
                acquire(order);
            }
            
            shared_ptr_header<T>* get_ptr()
            {
                return val.ptr;
            }
            
            shared_ptr<T> get_shared_ptr()
            {
                return shared_ptr<T>(val.ptr);
            }
            
        };
        
        
    public:
        void mark(bool val) {
            auto loc = p.load();
            p.compare_exchange_weak(loc, counted_ptr(loc.ptr, val, loc.access_count));
        }
        
        
        
        void store(
                   shared_ptr<T> newptr,
                   std::memory_order order= std::memory_order_seq_cst) /*noexcept*/
        {
            
            
            counted_ptr old=p.exchange(counted_ptr(newptr.header,newptr.mark),order);
            if(old.ptr){
                
                old.ptr->modify_count(-1,old.access_count);
            }
            newptr.clear();
        }
        
        shared_ptr<T> load(
                           std::memory_order order= std::memory_order_seq_cst) const noexcept
        {
            local_access guard(p,order);
            return guard.get_shared_ptr();
        }
        
        operator shared_ptr<T>() const noexcept {
            return load();
        }
        
        shared_ptr<T> exchange(
                               shared_ptr<T> newptr,
                               std::memory_order order= std::memory_order_seq_cst) /*noexcept*/
        {
            counted_ptr newval(
                               newptr.header,
                               newptr.mark);
            counted_ptr old=p.exchange(newval,order);
            shared_ptr<T> res(old.ptr);
            if(old.ptr){
                old.ptr->modify_count(-1,old.access_count);
            }
            newptr.clear();
            return res;
        }
        
        bool compare_exchange_weak(
                                   shared_ptr<T> & expected, shared_ptr<T> newptr,
                                   std::memory_order success_order=std::memory_order_seq_cst,
                                   std::memory_order failure_order=std::memory_order_seq_cst) /*noexcept*/
        {
            local_access guard(p);
            if(guard.get_ptr()!=expected.header){
                expected=guard.get_shared_ptr();
                return false;
            }
            counted_ptr oldval(guard.val);
            counted_ptr newval( newptr.header,newptr.mark );
            if(p.compare_exchange_weak(oldval,newval,success_order,failure_order)){
                if(oldval.ptr){
                    oldval.ptr->modify_count(-1,oldval.access_count);
                }
                newptr.clear();
                return true;
            }
            else{
                guard.refresh(oldval,failure_order);
                expected=guard.get_shared_ptr();
                return false;
            }
        }
        
        bool compare_exchange_strong(
                                     shared_ptr<T> &expected,shared_ptr<T> newptr,
                                     std::memory_order success_order=std::memory_order_seq_cst,
                                     std::memory_order failure_order=std::memory_order_seq_cst) noexcept {
            shared_ptr<T> local_expected=expected;
            do{
                if(compare_exchange_weak(expected,newptr,success_order,failure_order))
                    return true;
            }
            while(expected==local_expected);
            return false;
        }
        
        atomic_shared_ptr() /*noexcept*/ : atomic_shared_ptr(nullptr) { }
        atomic_shared_ptr( shared_ptr<T> val) /*noexcept*/:
        p(counted_ptr(val.header,val.mark)) {
            val.clear();
        }
        
        ~atomic_shared_ptr()
        {
            counted_ptr old=p.load(std::memory_order_relaxed);
            if(old.ptr)
                old.ptr->modify_count(-1,0);
        }
        
        atomic_shared_ptr(const atomic_shared_ptr&) = delete;
        atomic_shared_ptr& operator=(const atomic_shared_ptr&) = delete;
        shared_ptr<T> operator=(shared_ptr<T> newval) noexcept {
            store(static_cast<shared_ptr<T>&&>(newval));
            return newval;
        }
    };
} /* namespace */


#endif /* managed_tagged_ptr_h */
