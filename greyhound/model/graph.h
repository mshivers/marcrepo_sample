#pragma once


#include "model/histogram.h"
#include "model/node.h"
#include "model/serialize_utils.h"

#include <vector>
#include <set>
#include <unordered_set>

#include <lib/factory.h>
#include <lib/JSON.h>
#include <lib/memoize.h>
#include <lib/meta.h>
#include <lib/optional.h>
#include <lib/types.h>
#include <lib/vplat_time.h>
#include <lib/vplat_log.h>
#include <lib/spinlock.h>

#include <gtest/gtest_prod.h>

#include <chrono>

// create "has_create"
HAS_MEM_FUN(create)

struct SourceNode;
struct Strategy;
struct RawMarketData;

namespace data_grab {
    struct DataGrabber;
}

struct Graph {
    using wallClock = std::chrono::high_resolution_clock;
    using simClock = vplat_clock;
    
    using cleanup_fun = void(*)(void);

    std::set<cleanup_fun> cleanup_funs;
    std::set<Node*> nodes; 
    std::vector<Node::StatusCode> nodeStatus_;

    // Nodes that will be deserialized after the valuation and order logic.
    std::vector<ValueNode*> utilityNodes_;

    Graph(Strategy* strategyPtr=nullptr)
        : eventId_(0)
        , wallTStartFire_(wallClock::now())
        , wallTEndFire_(wallClock::now())
        , currentSource_(nullptr)
        , strategyPtr_(strategyPtr) 
        , histogram_(500, 10, 50000)
    {}
    
    Graph(Graph const& that) = delete;

    virtual ~Graph() {
        for(auto& fun : cleanup_funs)
            fun();
        cleanup_funs.clear();
    }

    Strategy* getStrategy() const;

    template<typename T>
    std::set<T*> getNodes() const {
        std::set<T*> r;
        for(Node* n: nodes)
            if(nullptr != dynamic_cast<T*>(n))
                r.insert(dynamic_cast<T*>(n));
        return r;
    }

    // This is called during vpl::onInitFinished
    void onInitFinished();

    bool hasCycleUtil(Node* node,
                  std::unordered_set<Node*>& visited,
                  std::unordered_set<Node*>& recursed);

    bool isCyclic() {
        std::unordered_set<Node*> visited; 
        std::unordered_set<Node*> recursed;
        for(auto node : nodes)
            if ( hasCycleUtil(node, visited, recursed) )
                return true;
        return false;
    }

    //verifies parent_/children_ and clocks_/callbacks_ are symmetric
    bool hasSymmetricEdges();

    int eventId() const {return eventId_;}
    
    bool valid() { return (!isCyclic() && hasSymmetricEdges()); }

    std::string graphViz();
    bool saveGraphViz(std::string fileName="graph");

    // list of event id where a graphViz will be saved
    std::vector<int>& graphVizEvent() {return graphVizEvent_;}

    //Called only from Node constructor
    void registerNode(Node* node) {
        nodes.insert(node);
    }

    std::vector<Node*> constructOrder_;
    void addToConstructOrder(Node* n) {
        if(std::find(constructOrder_.begin(), constructOrder_.end(), n) == constructOrder_.end())
            constructOrder_.push_back(n);
    }
    std::vector<Node*> const& constructOrder() {return constructOrder_;}
    template <typename T, typename... Args> 
    T* add(Args... args) {
        static_assert(std::is_base_of<Serializable, T>::value, "T is not derived from Serializable");
        static_assert(has_create<T, Graph*, Args...>::value, "T does not have a create function");
        auto memoizedCreate = memoize(utils::getCreateFunc<T>());
        auto item = memoizedCreate(this, std::forward<Args>(args)...);
        addToConstructOrder(item);
        cleanup_funs.insert(&Graph::clear_type_cache<T, Args...>);
        assert(item);
        return item;
    }
    
    template <typename T, typename... Args>
    static void clear_type_cache(void) {
        static_assert(std::is_base_of<Serializable, T>::value, "T is not derived from Serializable");
        memoize_clear(utils::getCreateFunc<T>());
    }
    
    // The functions below allow deserializing a node from json.
    using MakeType = std::function<Serializable*(Graph* g, Parameters const&)>;
    using Table = std::map<std::string, MakeType>;

    static bool register_type(std::string const& key, MakeType const& make) {
        Table& tab = table();
        if(tab.count(key))
            throw ConfigError("Trying to register duplicate node: " + key);
        tab[key] = make;
        return true;
    }

    static auto find_type(std::string const& key) {
        Table& tab = table();
        auto result = tab.find(key);
        if ( tab.end() == result ) {
            std::ostringstream oss;
            oss << "Cannot find type [" << key << "] (available: ";
            bool first = true;
            for (auto pair : tab) {
                if ( first ) first = false;
                else oss << ", ";
                oss << pair.first;
            }
            oss << ')';
            throw std::logic_error(oss.str().c_str());
        }
        return result->second;
    }

    std::string deserLogIndent_;
    template<typename T>
    T* deserialize(Parameters const& p) {
        auto type = p["type"].get<std::string>();

        LOG_INFO() << "Graph:deserializing: " << deserLogIndent_ << type;
        deserLogIndent_ += " ";
        
        auto deserializer = Graph::find_type(type);
        Serializable* rawStruct = deserializer(this, p);
        if ( !rawStruct )
            throw std::logic_error("Graph::deserialize: Node not found.");
        
        T* node = dynamic_cast<T*>(rawStruct);
        if ( !node ) {
            std::string errMsg = "Graph::deserialize: Wrong node subtype requested.\n";
            errMsg += "Deserialized node is: " + rawStruct->getClassName() + "\n";
            errMsg += "Requested typeid is: "+ std::string(typeid(T).name()) + "\n";
            
            throw std::logic_error(errMsg);
        }
        
        LOG_INFO() << "...with name " << deserLogIndent_ << node->getName();
        deserLogIndent_.pop_back();
        return  node;
    }

    Node* firingNode();

    simClock::time_point getStartFireTime(){
        return startFireTime_;
    }

    simClock::time_point getStartTime(){
        return startNSec_;
    }

    void notifyPreFire(SourceNode* source);
    void notifyPostFire();

    SourceNode const* currentSource() const {
        return currentSource_;
    }

    int64_t nSecUptime() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(uptime_).count(); 
    }

    void setDataGrabber(data_grab::DataGrabber*);
    void addUtilityNode(ValueNode*);
    std::vector<ValueNode*> utilityNodes() {return utilityNodes_;}
    void loadUtilityNodes(JSON const&);
    
    lib::spinlock mutex_;    

    void addNodeToAudit(Node* node) { nodesToAudit_.push_back(node); }
    void nodeAudit(Node* node);

    private:
    friend struct Strategy; // To be able to set strategyPtr_
    int eventId_;
    wallClock::duration uptime_;
    simClock::time_point startNSec_, startFireTime_;
    wallClock::time_point wallTStartFire_, wallTEndFire_;
    SourceNode* currentSource_;

    Strategy* strategyPtr_;
    void setStrategy(Strategy* strategy);

    Histogram histogram_;
    std::vector<int> graphVizEvent_;
    std::vector<Node*> nodesToAudit_;
    
    static Table& table() {
        static Table* p = new Table;
        return *p;
    }
};

//Function templates for graph traversal required by SourceNode:
//Note: this DFS implementation works for cyclic graphs as well
template <typename Fun, typename ...Args>
void applyDepthFirstImpl(Node* node, std::set<Node*>& visited, 
         bool applyToAllChildren, Fun func, Args&... args) {
    if ( visited.find(node) != visited.end() )
        return;
    visited.emplace(node);

    std::set<Node*> children(node->callbacks_.begin(),
                             node->callbacks_.end());
    if ( applyToAllChildren )
        children.insert(node->children_.begin(), node->children_.end());
    
    for(auto otherNode : children)
        applyDepthFirstImpl(otherNode, visited, applyToAllChildren,
                            func, args...);

    func(node, args...);
    return;
}

template <typename Fun, typename ...Args>
void traverseCallbacks(Node* root, Fun func, Args&... args) {
    std::set<Node*> visited;
    applyDepthFirstImpl(root, visited, false, func, args...);
}

template <typename Fun, typename ...Args>
void traverseChildren(Node* root, Fun func, Args&... args) {
    std::set<Node*> visited;
    applyDepthFirstImpl(root, visited, true, func, args...);
}

template<typename FUNC>
void applyToDependencies(Node* root, FUNC f)
{
    std::unordered_map<Node*, std::set<Node*>> prerequisit;
    for(auto n: root->getGraph()->nodes)
    {
        for(auto c: n->callbacks())
            prerequisit[c].insert(n);
        for(auto c: n->children())
            prerequisit[c].insert(n);
    }
    std::set<Node*> visited;
    std::function<void(Node*)> applyImpl; // Recursive lambda: https://stackoverflow.com/a/4081391
    applyImpl = [&](Node*n) -> void
        {
            if(visited.count(n))
                return;
            visited.insert(n);
            for(auto dep: prerequisit[n])
                applyImpl(dep);
            f(n);
        };
    applyImpl(root);
    return;
}

inline
void topological_sort(Node* root, std::vector<Node*>& order,
         lib::optional<std::set<Node*>> onlyInclude=lib::optional<std::set<Node*>>()) {
    auto appendNode = [&order, &onlyInclude] (Node* node) {
        if ( onlyInclude ) {
            if ( onlyInclude.get().count(node) )
                order.emplace_back(node);
        } else {
            order.emplace_back(node);
        }
    };
    
    order.clear();    
    traverseChildren(root,appendNode);
    std::reverse(order.begin(), order.end());
}

template <typename Set> 
void addAllChildren(Set& set, Node* root) {
    traverseChildren(root, [&set](Node* node) {set.insert(node);});
}

template <typename Set> 
void addAllCallbacks(Set& set, Node* root) {
    traverseCallbacks(root, [&set](Node* node) {set.insert(node);});
}

template <typename Range, typename Fun, typename ...Args>
void for_each(Range& range, Fun&& fun, Args&&... args) {
    for (auto& thing : range)
        if ( thing ) fun(thing, std::forward<Args&&>(args)...);
}

template <typename Set>
void addDirectCallbacks(Set& set, Node* node) {
    for_each(node->callbacks_, [&set](Node* node) {set.insert(node);});
}


struct SourceNode : public ClockNode {
    SourceNode(Graph* g) 
        : ClockNode(g), currentNode_(nullptr) {
        treeUpdated();
    }
    virtual ~SourceNode() = default;

    //This must be called whenever a node changes its children_ or callbacks_.
    virtual void treeUpdated() {
        computeOrder_.clear();
        std::set<Node*> callbacks;
        std::vector<Node*> fullSort;
        addAllCallbacks(callbacks, this);
        topological_sort(this, fullSort, callbacks);
        assert(*fullSort.begin() == this);
        for( auto node : fullSort )
            if ( node != this )
                computeOrder_.emplace_back(node);
    }

    //Note the timing of the reset. If everything is reset after firing, that's
    //more efficient, but in that case node->value() will always fail in tests,
    //because node->ticked_ has already been reset to false. So in DEBUG we
    //wait until the beginning of the next firing to reset the entire graph. 
    void fire() override {
        assert(getGraph()->getStrategy() == nullptr || // OK: we are doing tests
               getGraph()->mutex_.locked.test_and_set(std::memory_order_acquire));
        
        ++nFired;
        ++nComputed;
        ++nTicked;
        ++nTickedTrue;
        
        // In debug, reset before firing, so ticked_ remains viewable after this call.
        // The nodes that need reseting are the nodes in the computeOrder_ of the
        // last source node (and that node) that fired. But as we don't know which are those node
        // we reset all the nodes in the graph.
        #ifndef NDEBUG 
        for (auto node : getGraph()->nodes)
            node->reset();
        #endif

        status_ = StatusCode::OK;
        ticked_ = true;
        getGraph()->notifyPreFire(this);
        for(auto node : computeOrder_)
        {
            currentNode(node);
            node->fire();
        }
        currentNode(nullptr);
        getGraph()->notifyPostFire();

        // Reset after firing in prod for efficiency
        // Contrarlily to the debug case, we know the only nodes that need
        // reseting are the ones in the computeOrder_ and this one.
        #ifdef NDEBUG
        for (auto node : computeOrder_)  
            node->reset();
        this->reset();
        #endif
    }


    ClockSet getSourceClockSet() override final { return ClockSet{this}; }

    std::vector<Node*> computeOrder_;

    Node* currentNode() {return currentNode_;}
    void currentNode(Node* n) {currentNode_=n;}
    private:
    Node* currentNode_;
    
    virtual void compute() override final {
        // Compute should never be called.
        throw std::logic_error("Unexpected call.");
    } 
};
