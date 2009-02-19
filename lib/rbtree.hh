#ifndef bqtRbTreeHH
#define bqtRbTreeHH

/*
This is Bisqwit's Red-Black tree implementation for replacing
std::multimap<> on those relevant parts that are used in mkcromfs.

Why another rbtree implementation? Because GCC's multimap cannot
handle custom "pointer" type, if your allocator happens to use those!

To save virtual memory in Cromfs, we use 32-bit pointers here even when
we're on a 64-bit platform. This is supposed to be combined with
"allocatorNk" also included here.

As for the rotate/fix functions, it is a rather straightforward copy
from those in Introduction to Algorithms.

In the code you see a lot of things like this:
  pointer(x->left)->value
Why that, and not simply x->left->value? This
is because of a certain chicken-and-egg problem.

To define tree nodes, you need to know the pointer type.
Because each node contains a few pointers.

You get the pointer type from the allocator. So you need:
  Node<Allocator>
This allows the Node to be specialized for the allocator.

To define the allocator, you need to know what your nodes
are like. Otherwise, you don't know what you're allocating.
So you need:
  Allocator<Node>

In C++, you cannot have Node<Allocator<Node<Allocator<Node...>>>>.
This is where the problem comes from. So we approximate as close
as we can and do the rest with casting. This however makes the
tree incompatible with standard allocators. Oh well.

*/

namespace RbTreeNs
{
    enum ColorType { R,B };
};

#define rbtree_printf(x...) /**/
//#define rbtree_printf printf

template<typename ElemT, typename AllocT>
struct RbTreeNode
{
    typedef typename AllocT::pointer pointer;

    pointer  left, right, parent;
    ElemT    value;
    RbTreeNs::ColorType color;

    RbTreeNode() : left(0),right(0),parent(0), value(), color(RbTreeNs::R)
    {
    }
    RbTreeNode(const ElemT& v) : left(0),right(0),parent(0), value(v), color(RbTreeNs::R)
    {
    }
};

namespace RbTreeNs
{
    template<typename PointerT>
    PointerT minimum(PointerT x)
    {
        while(x->left) x = PointerT(x->left);
        return x;
    }

    template<typename PointerT>
    PointerT maximum(PointerT x)
    {
        while(x->right) x = PointerT(x->right);
        return x;
    }

    template<typename PointerT>
    PointerT next(PointerT x)
    {
        if(x->right) return minimum(PointerT(x->right));

        PointerT y ( x->parent );
        while(y && PointerT(y->right) == x)
            { x = y; y = PointerT(y->parent); }
        return y;
    }

    /* NOTE: Prev(end()) does not work, because end() is nil.
     */
    template<typename PointerT>
    PointerT prev(PointerT x)
    {
        if(x->left) return maximum(PointerT(x->left));

        PointerT y ( x->parent );
        while(y && PointerT(y->left) == x)
            { x = y; y = y->parent; }
        return y;
    }
};

template<typename ValT, typename PtrT>
struct RbTreeIterator
{
    typedef ValT* pointer;
    typedef ValT& reference;

    typedef RbTreeIterator<ValT, PtrT> self;

    PtrT  node;

    RbTreeIterator() : node() { }
    explicit RbTreeIterator(PtrT p) : node(p) { }

    reference operator *() const { return  node->value; }
    pointer   operator->() const { return &node->value; }

    self& operator++() { node = RbTreeNs::next(node); return *this; }
    self& operator--() { node = RbTreeNs::prev(node); return *this; }

    bool operator==(const self& x) const { return node == x.node; }
    bool operator!=(const self& x) const { return !operator==(x); }
};

template<typename ValT, typename PtrT>
struct RbTreeConstIterator
{
    typedef const ValT* const_pointer;
    typedef const ValT& const_reference;

    typedef RbTreeConstIterator<ValT, PtrT> self;

    PtrT   node;

    RbTreeConstIterator() : node() { }
    explicit RbTreeConstIterator(PtrT p) : node(p) { }

    const_reference operator *() const { return  node->value; }
    const_pointer   operator->() const { return &node->value; }

    self& operator++() { node = RbTreeNs::next(node); return *this; }
    self& operator--() { node = RbTreeNs::prev(node); return *this; }

    bool operator==(const self& x) const { return node == x.node; }
    bool operator!=(const self& x) const { return !operator==(x); }
};

template<typename ValT, typename PtrT>
static inline bool operator==
    (const RbTreeIterator<ValT,PtrT>& a,
     const RbTreeConstIterator<ValT,PtrT>& b)
    { return a.node == b.node; }

template<typename ValT, typename PtrT>
static inline bool operator==
    (const RbTreeConstIterator<ValT,PtrT>& a,
     const RbTreeIterator<ValT,PtrT>& b)
    { return a.node == b.node; }

template<typename ValT, typename PtrT>
static inline bool operator!=
    (const RbTreeIterator<ValT,PtrT>& a,
     const RbTreeConstIterator<ValT,PtrT>& b)
    { return a.node != b.node; }

template<typename ValT, typename PtrT>
static inline bool operator!=
    (const RbTreeConstIterator<ValT,PtrT>& a,
     const RbTreeIterator<ValT,PtrT>& b)
    { return a.node != b.node; }

template<typename KeyT, typename ValT,
         typename KeyOfValue,
         typename Compare,
         typename Alloc>
struct RbTree
{
    typedef RbTreeNode<ValT, Alloc> NodeType_tmp0;
    typedef typename Alloc::template rebind<NodeType_tmp0>::other AllocT_tmp5;
    typedef RbTreeNode<ValT, AllocT_tmp5> NodeType_tmp6;
    typedef typename Alloc::template rebind<NodeType_tmp6>::other AllocT_tmp6;
    typedef RbTreeNode<ValT, AllocT_tmp6> NodeType;
    typedef typename Alloc::template rebind<NodeType>::other AllocT;

    typedef typename AllocT::pointer pointer;
    typedef typename AllocT::const_pointer const_pointer;
    typedef typename AllocT::reference reference;
    typedef typename AllocT::const_reference const_reference;

    typedef RbTreeIterator<ValT, pointer> iterator;
    typedef RbTreeConstIterator<ValT, const_pointer> const_iterator;

    pointer root;
/*
    static struct nil_holder
    {
        nil_holder()
        {
            p = AllocT().allocate(1);
            AllocT().construct(p, v);
        }
        pointer p;

        operator pointer() const { return p; }
    } nil;
*/
public:
    RbTree() : root() {}

    RbTree(const RbTree& b) : root() { insert(b.begin(), b.end()); }

    void insert(const ValT& v)
    {
        pointer p = AllocT().allocate(1);
        rbtree_printf("Inserting %p\n", (void*)p);
        AllocT().construct(p, v);
        Insert(p);
    }

    void insert(const_iterator a, const_iterator b)
    {
        while(a != b) { insert(*a); ++a; }
    }

    void erase(iterator a)
    {
        rbtree_printf("ERASING %p\n", (void*)a.node);
        Delete(a.node);
        AllocT().destroy(a.node);
        AllocT().deallocate(a.node, 1);
    }

    void clear()
    {
        erase(begin(), end());
    }

    void swap(RbTree& b)
    {
        pointer r = root;
        root = b.root;
        b.root = r;
    }

    iterator lower_bound(const KeyT& v) { return lower_bound(begin(),end(),v); }
    //iterator upper_bound(const KeyT& v) { return upper_bound(begin(),end(),v); }

    const_iterator lower_bound(const KeyT& v) const { return lower_bound(begin(),end(),v); }
    //const_iterator upper_bound(const KeyT& v) const { return upper_bound(begin(),end(),v); }

    template<typename K>
    const_iterator lower_bound(const_iterator xi, const_iterator yi, const K& k) const
    {
        const_pointer x ( xi.node ), y ( yi.node );
        while(x)
        {
            if(IsLess(x, k))
                { y = x; x = pointer(x->left); }
            else
                { x = pointer(x->right); }
        }
        return const_iterator(y);
    }
    template<typename K>
    iterator lower_bound(iterator xi, iterator yi, const K& k)
    {
        pointer x ( xi.node ), y ( yi.node );
        while(x)
        {
            if(IsLess(x, k))
                { y = x; x = pointer(x->left); }
            else
                { x = pointer(x->right); }
        }
        return iterator(y);
    }

    template<typename K>
    iterator find(const K& k)
    {
        iterator r = lower_bound(k);
        if(r != end() && KeyOfValue()(*r) != k) return end();
        return r;
    }
    template<typename K>
    const_iterator find(const K& k) const
    {
        const_iterator r = lower_bound(k);
        if(r != end() && KeyOfValue()(*r) != k) return end();
        return r;
    }

    iterator       begin() { return iterator(root); }
    const_iterator begin() const { return const_iterator( const_pointer(root) ); }
    iterator       end() { return iterator(pointer()); }
    const_iterator end() const { return const_iterator(const_pointer()); }

private:
    static RbTreeNs::ColorType Color(pointer x)
    {
        return x ? x->color : RbTreeNs::B;
    }

    void RotateLeft(pointer x)
    {
        pointer y ( x->right );
        x->right  = y->left;
        if(y->left) pointer(y->left)->parent = x;
        y->parent = x->parent;

        if(x == root)
            root = y;
        else if(x == pointer(x->parent)->left)
            pointer(x->parent)->left = y;
        else
            pointer(x->parent)->right = y;
        y->left = x;
        x->parent = y;
    }
    void RotateRight(pointer x)
    {
        pointer y ( x->left );
        x->left   = y->right;
        if(y->right) pointer(y->right)->parent = x;
        y->parent = x->parent;
        if(x == root)
            root = y;
        else if(x == (x->parent)->right)
            (x->parent)->right = y;
        else
            (x->parent)->left = y;
        y->right = x;
        x->parent = y;
    }

    void Dump(const char* label)
    {
        rbtree_printf("Dump %s\n", label);
        Dump(root, 2);
    }
    void Dump(pointer node, int level)
    {
        rbtree_printf("%*s", level, "");
        if(!node)
            rbtree_printf("<nil> (black)\n");
        else
        {
            rbtree_printf("%p, parent %p (%s)\n",
                (void*)node, (void*)node->parent,
                node->color == RbTreeNs::R ? "red" : "black");
            Dump(node->left,  level+2);
            Dump(node->right, level+2);
        }
    }

    void Insert(pointer z)
    {
        //Dump("before insert");

        pointer y ( 0 ), x ( root );
        while(x)
        {
            y = x;
            //rbtree_printf("isless(%p,%p)\n", (void*)z, (void*)x);
            if(IsLess(z, x))
                x = x->left;
            else
                x = x->right;
        }
        z->parent = y;
        if(!y)
            root = z;
        else if(IsLess(z,y))
            y->left = z;
        else
            y->right = z;
        // color is red due to being recently constructed
        InsertFixup(z);

        //Dump("after insert");
    }

    void InsertFixup(pointer z)
    {
        // If the parent is red, the rule "red's children must be black" is violated.
        while(Color(z->parent) == RbTreeNs::R)
        {
            //Dump("in fixup loop");
            pointer zpp ( pointer(z->parent)->parent );

            if(z->parent == zpp->left)
            {
                pointer y ( zpp->right );
                if(Color(y) == RbTreeNs::R)
                {
                    pointer(z->parent)->color = RbTreeNs::B;
                    y->color = RbTreeNs::B;
                    zpp->color = RbTreeNs::R;
                    z = zpp;
                }
                else
                {
                    if(z == pointer(z->parent)->right)
                    {
                        z = z->parent;
                        RotateLeft(z);
                    }
                    pointer(z->parent)->color = RbTreeNs::B;
                    zpp->color = RbTreeNs::R;
                    RotateRight(zpp);
                }
            }
            else
            {
                pointer y ( zpp->left );
                if(Color(y) == RbTreeNs::R)
                {
                    pointer(z->parent)->color = RbTreeNs::B;
                    y->color = RbTreeNs::B;
                    zpp->color = RbTreeNs::R;
                    z = zpp;
                }
                else
                {
                    if(z == pointer(z->parent)->left)
                    {
                        z = z->parent;
                        RotateRight(z);
                    }
                    pointer(z->parent)->color = RbTreeNs::B;
                    zpp->color = RbTreeNs::R;
                    RotateLeft(zpp);
                }
            }
        }
        root->color = RbTreeNs::B;
    }

    pointer Delete(pointer z)
    {
        pointer y (
            (!z->left || !z->right)
            ? z
            : RbTreeNs::next(z) );
        pointer x (
            y->left
            ? y->left
            : y->right );
        x->parent = y->parent;
        if(!y->parent)
            root = x;
        else if(y == pointer(y->parent)->left)
            pointer(y->parent)->left = x;
        else
            pointer(y->parent)->right = x;
        if(y != z)
            z->value = y->value;
        if(y->color == RbTreeNs::B)
            DeleteFixup(x);
        return y;
    }

    void DeleteFixup(pointer x)
    {
        while(x != root && Color(x) == RbTreeNs::B)
        {
            if(x == pointer(x->parent)->left)
            {
                pointer w ( pointer(x->parent)->right );
                if(Color(w) == RbTreeNs::R)
                {
                    w->color = RbTreeNs::B;
                    pointer(x->parent)->color = RbTreeNs::R;
                    RotateLeft(x);
                    w = pointer(x->parent)->right;
                }
                if(Color(w->left) == RbTreeNs::B && Color(w->right) == RbTreeNs::B)
                {
                    w->color = RbTreeNs::R;
                    x->parent = x;
                }
                else
                {
                    if(Color(w->right) == RbTreeNs::B)
                    {
                        pointer(w->left)->color = RbTreeNs::B;
                        w->color = RbTreeNs::R;
                        RotateRight(w);
                        w = pointer(x->parent)->right;
                    }
                    w->color = pointer(x->parent)->color;
                    pointer(x->parent)->color = RbTreeNs::B;
                    pointer(w->right)->color = RbTreeNs::B;
                    RotateLeft(x);
                    x = root;
                }
            }
            else
            {
                pointer w ( pointer(x->parent)->left );
                if(Color(w) == RbTreeNs::R)
                {
                    w->color = RbTreeNs::B;
                    pointer(x->parent)->color = RbTreeNs::R;
                    RotateRight(x);
                    w = pointer(x->parent)->left;
                }
                if(Color(w->right) == RbTreeNs::B && Color(w->left) == RbTreeNs::B)
                {
                    w->color = RbTreeNs::R;
                    x->parent = x;
                }
                else
                {
                    if(Color(w->left) == RbTreeNs::B)
                    {
                        pointer(w->right)->color = RbTreeNs::B;
                        w->color = RbTreeNs::R;
                        RotateLeft(w);
                        w = pointer(x->parent)->left;
                    }
                    w->color = pointer(x->parent)->color;
                    pointer(x->parent)->color = RbTreeNs::B;
                    pointer(w->left)->color = RbTreeNs::B;
                    RotateRight(x);
                    x = root;
                }
            }
        }
        x->color = RbTreeNs::B;
    }

    bool IsLess(pointer z, pointer x) const
    {
        return Compare()(
            KeyOfValue() (z->value),
            KeyOfValue() (x->value)
        );
    }
    bool IsLess(pointer z, const KeyT& k) const
    {
        return Compare()(
            KeyOfValue() (z->value),
            k
        );
    }
    bool IsLess(const_pointer z, const_pointer x) const
    {
        return Compare()(
            KeyOfValue() (z->value),
            KeyOfValue() (x->value)
        );
    }
    bool IsLess(const_pointer z, const KeyT& k) const
    {
        return Compare()(
            KeyOfValue() (z->value),
            k
        );
    }
};

#endif
