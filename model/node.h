#pragma once

#include "model/serialize_utils.h"

#include <set>
#include <vector>
#include <limits>

#include <lib/factory.h>
#include <lib/JSON.h>
#include <lib/memoize.h>
#include <lib/meta.h>
#include <lib/optional.h>
#include <lib/str_utils.h>
#include <lib/types.h>
#include <lib/vplat_log.h>
#include <gtest/gtest_prod.h>

struct Node;
struct Graph;
struct ClockNode;
struct ValueNode;
struct CodeGenAccessor;                                      

using ClockSet = std::set<ClockNode*>;
using NodeSet = std::set<Node*>;
using Value = double;
using Parameters = JSON;

bool inSameGraph(Node const* first, Node const* second);

struct Node : Serializable {
    enum class StatusCode {OK=0, INIT, INVALID, ERROR, FATAL};
    using vector = std::vector<Node*>;
    
    static unsigned int count;
    
    Node(Graph* graph);
    virtual ~Node() = default;
    Node(Node const& that) = delete;

    void setParent(Node* parent) {
        assert(inSameGraph(parent, this));
        if ( hasParent(parent)==false ) {
            parents_.emplace_back(parent);
            parent->children_.emplace_back(this);
            auto kids = parent->children_;
            assert(std::count(kids.begin(), kids.end(), this)==1);
            treeUpdated();
        }
    }

    template<typename T, typename... Args>
    void setParents(T first, Args... others) {
        NodeSet nodes = combineNodes(first, others...);
        for(auto node : nodes) setParent(node);
    }
 
    void addParent(Node* parent) { setParent(parent); }

    void setClock(Node* clock) {
        NodeSet clockSet{clock};
        setClockImpl(clockSet);
    }

    template<typename T, typename... Args>
    void setClock(T first, Args... others) {
        NodeSet nodes = combineNodes(first, others...);
        setClockImpl(nodes);
    }
 
    virtual ClockSet getSourceClockSet();
    virtual void setClockImpl(const NodeSet& nodes);
    virtual void treeUpdated(); //notify SourceNode to recompute firing order
    virtual void compute() = 0;
    virtual void fire() = 0;
    StatusCode status() const { return status_; }
    bool ticked() { return ticked_; };
    void reset() { ticked_ = false; } //called after each event loop.
    void setOK() { status_ = StatusCode::OK; }
    bool valid() const { return status_ == StatusCode::OK; }
    bool parentsValid();

    virtual ClockNode* getClock() { 
        throw std::logic_error("This will be pure virtual soon; please implement");
    } 

    bool hasParent(Node* node) { // assertion helper function
        return std::find(parents_.begin(), parents_.end(), node)!=parents_.end();
    }

    bool hasClock(Node* node) { // assertion helper function
        return std::find(clocks_.begin(), clocks_.end(), node)!=clocks_.end();
    }

    template <typename T>
    bool isType() { return dynamic_cast<T*>(this)!=nullptr; }

    virtual std::string getClassName() const {
        std::string r("NODE -- typeid = ");
        r += typeid(*this).name();
        return r;
    }

    virtual std::string defaultName() const { return getClassName(); }
    std::string getName() const { return name_.empty() ? defaultName() : name_; }

    void setName(const std::string& name, bool force=false) {
        if(not force and isNameSet() and name != getName())
            throw std::logic_error(("Trying to reset node name to: " + name + ". Already set to : " + getName()).c_str());
        name_ = name;
    }
    bool isNameSet() { return not name_.empty(); }

    virtual Parameters serialize() const {
        std::string msg = "serialize not implemented for class: ";
        msg += getClassName();
        throw NotImplemented(msg);
    }

    Graph*       getGraph()       { return graph_; }
    Graph const* getGraph() const { return graph_; }

    auto const& getClocks() const { return clocks_; }
    auto&       getClocks()       { return clocks_; }

    std::vector<Node*> const& callbacks() const {return callbacks_;}
    std::vector<Node*> const& parents() const {return parents_;}
    std::vector<Node*> const& children() const {return children_;}
    
    auto numParents() { return parents_.size(); }
    auto numChildren() { return children_.size(); }
    auto numClocks() { return clocks_.size(); }
    auto numCallbacks() { return callbacks_.size(); }

    int id() const {return id_;}

    virtual void audit() {}
    
    protected:
    Graph* graph_;
    const int id_;
    StatusCode status_{StatusCode::INIT}; 
    bool ticked_{false};
    std::vector<ClockNode*> clocks_;
    std::vector<Node*> callbacks_;
    std::vector<Node*> parents_;
    std::vector<Node*> children_;
    std::string defaultName_, name_;
    int nFired;
    int nTicked;
    int nComputed;
    
    friend Graph;
    friend CodeGenAccessor;                                      

    template <typename Fun, typename ...Args>
    friend void applyDepthFirstImpl(
        Node*, std::set<Node*>&, bool, Fun, Args&... );

    FRIEND_TEST(test_graph, factory_dtor);
    FRIEND_TEST(test_order_logic, deserialize);
};

std::ostream& operator<<(std::ostream& os, Node::StatusCode const& s);

//Base class for all clock-type nodes
struct ClockNode : public Node {
    ClockNode(Graph* g) : Node(g) {
            nTickedTrue = 0;
        }
    virtual ~ClockNode() = default;
    ClockNode(ClockNode const& that) = delete;

    virtual ClockNode* getClock() override final { return this; }
      
    virtual void setClockImpl(const NodeSet& nodes) override final {
        for(auto node : nodes) {
            auto clock = node->getClock();
            assert(inSameGraph(clock, this));
            if ( hasClock(clock)==false ) {
                clocks_.emplace_back(clock);
                clock->callbacks_.emplace_back(this);
                auto cbs = clock->callbacks_;
                assert(std::count(cbs.begin(), cbs.end(), this)==1);
            }
            if ( node->isType<ClockNode>()==false ) {
                addParent(node);
            }
        }
        treeUpdated();
    }

    virtual void fire() override {
        ++nFired;
        for(auto clock : clocks_) {
            if ( clock->ticked() ) {
                ++nTicked;
                if ( parentsValid() ) {
                    compute();
                    ++nComputed;
                    nTickedTrue += ticked_;
                    if (not valid()) LOG_INFO() << "Node invalid after compute() with parents all valid:  " << defaultName();
                } else {
                    if ( valid() ) //don't change status if INIT or other non-OK
                        status_ = StatusCode::INVALID;
                }
                return;
            }
        }
    }

    int nTickedTrue;
};

template<typename T>
NodeSet combineNodesImpl(T* node) {
    NodeSet nodes;
    nodes.insert(node);
    return nodes;
}

template<typename T>
NodeSet combineNodesImpl(const std::set<T*>& nodes) {
    return NodeSet{nodes.begin(), nodes.end()};
}

template<typename T>
NodeSet combineNodesImpl(const std::vector<T*>& nodes) {
    return NodeSet{nodes.begin(), nodes.end()};
}

NodeSet combineNodes();

template<typename T, typename... Args>
NodeSet combineNodes(T first, Args... args) {
    NodeSet combined = combineNodesImpl(first);
    auto remaining = combineNodes(args...);
    combined.insert(remaining.begin(), remaining.end());
    return combined;
}

bool hasCompatibleUnits(ValueNode* lhs, ValueNode* rhs);

//Base class for all value-type nodes
struct ValueNode : public Node {
    enum class Units { TICKS, INCREASE, PRICE, SIZE, NONE};
    using vector = std::vector<ValueNode*>;
    ValueNode(Graph* g, Units units=Units::NONE) 
        : Node(g)
        , value_(std::numeric_limits<Value>::max())
        , units_(units)
    { }

    virtual ~ValueNode() = default;
    ValueNode(ValueNode const& that) = delete;

    static Units convertUnits(const std::string&);
    Units units() const; 
    bool hasUnits() const;
    bool isTick() const; 
    bool isIncrease() const; 
    bool isPrice() const; 
    bool isSize() const; 
    Value value();
    Value heldValue() const;
    ClockNode* getClock() override final; 
 
    virtual void fire() override final {
        ++nFired;
        if ( getClock()->ticked() ) { 
            ticked_ = true;
            ++nTicked;
            if ( parentsValid() ) {
                ++nComputed;
                compute(); 
                if (not valid()) LOG_INFO() << "Node invalid after compute() with parents all valid:  " << defaultName();
            } else {
                //only change status if it's currently OK.
                //if it's currently INIT, changing it will FUBAR stuff
                if ( valid() ) {
                    status_ = StatusCode::INVALID;
                }
            }
        }
    }
    
    protected:
    Value value_;
    const Units units_;
};

struct MarketData;

struct Theo : ValueNode {
    std::string symbol() const;
    std::string shortSymbol() const;
    std::string defaultName() const override; 
    MarketData* marketData();

    MarketData* market_data_;
    Theo(Graph* g, std::string symbol);
    Theo(Graph* g, MarketData* market_data);
};

struct IncreasingNode : public ValueNode {
    #ifndef NDEBUG
    double last_value_;
    #endif
    
    IncreasingNode(Graph* g, Units units)
        : ValueNode(g, units) 
        #ifndef NDEBUG
        , last_value_(std::numeric_limits<double>::lowest())
        #endif
    {}

    void compute() final {
        computeIncreasing();
        
        #ifndef NDEBUG
        if ( last_value_ > value() )
            throw std::logic_error("IncreasingNode found to be decreasing...abort.");
        last_value_ = value();
        #endif
    }

    protected:
    virtual void computeIncreasing() = 0;
};

template <typename T>
Parameters serialize(const std::vector<T*>& nodes) {
    Parameters param;
    for(const auto n: nodes)
        param.push_back(n->serialize());
    return param;
}

