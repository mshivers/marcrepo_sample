#include "node.h"
#include "graph.h"
#include "clocks.h"
#include "market_data.h"

unsigned int Node::count = 0;

Node::Node(Graph* g)
    : graph_(g)
    , id_(Node::count++)
    , name_("")
    , nFired(0)
    , nTicked(0)
    , nComputed(0)
{
    g->registerNode(this);
}

std::ostream& operator<<(std::ostream& os, Node::StatusCode const& s) {
    switch(s) {
    case Node::StatusCode::INIT:     return os << "INIT";
    case Node::StatusCode::OK:       return os << "OK";
    case Node::StatusCode::INVALID:  return os << "INVALID";
    case Node::StatusCode::ERROR:    return os << "ERROR";
    case Node::StatusCode::FATAL:    return os << "FATAL";
    default: throw std::runtime_error("Unknown StatusCode");
    }
}

bool inSameGraph(Node const* first, Node const* second) {
    return first->getGraph() == second->getGraph();
}

void Node::treeUpdated() {
    for(auto clock : clocks_)
        clock->treeUpdated();
}

bool Node::parentsValid() { 
    for(auto parent : parents_)
        if(not parent->valid())
            return false;
    return true;
}

ClockSet Node::getSourceClockSet() {
    if(clocks_.size()==0) {
        std::string errMsg = getClassName() + "::getSourceClockSet : no clocks found\n";
        throw std::logic_error(errMsg);
    }

    ClockSet sourceClock;
    for(auto clock : clocks_) {
        auto source = clock->getSourceClockSet();
        sourceClock.insert(source.begin(), source.end());
    }
    return sourceClock;
}

void Node::setClockImpl(const NodeSet& nodes) {
    ClockNode* clock = joinClocks(nodes);  
    assert(inSameGraph(clock, this));

    if ( hasClock(clock)==false ) {
        clocks_.emplace_back(clock);
        clock->callbacks_.emplace_back(this);
        auto cbs = clock->callbacks_;
        assert(std::count(cbs.begin(), cbs.end(), this)==1);
    }     
    assert(clocks_.size()==1);

    //any non-clock nodes, and redundant clocks should be added as parents
    //to make sure they're calculated first.  ClockNodes are automatically sorted 
    //properly in the topological sort, but if you add a ValueNode as a clock, it 
    //will extract the clock only, so the ValueNode could be calculated after this
    //Node, if we don't explicitly add it as a parent.
    for(auto node : nodes) {
        if ( hasClock(node) == false ) 
            addParent(node);
    }
    treeUpdated();
}

Value ValueNode::heldValue() const
{
    #ifndef NDEBUG
    if(not valid())
        throw std::logic_error(getClassName() + "::value : "
            "Node's value is invalid when calling heldValue. ");
    #endif

    #if 0 //ndef NDEBUG
    auto firing = graph_->firingNode();
    if(firing)
    {
        bool isThisInChildren = false;
        auto findThis = [&](Node* n){isThisInChildren |= (n==this);};
        traverseChildren(const_cast<ValueNode*>(this), findThis);
        if(!isThisInChildren)
        {
            std::string errMsg("ValueNode::heldValue : calling value on node that is not your parent");
            errMsg += "\nFiring node: " + graph_->firingNode()->getClassName();
            errMsg += "\nValueNode:: " + getClassName();
            throw std::runtime_error(errMsg);
        }
    }
    #endif
    return value_;
}
Value ValueNode::value() {
    #ifndef NDEBUG
    if(not ticked())
        throw std::logic_error(getClassName() + "::value : "
            "Node's value is not current. " 
            "If this is expected, use heldValue instead");
    if(not valid())
        throw std::logic_error(getClassName() + "::value : "
            "Node's value is invalid when calling value. ");
    #endif
    return heldValue();
}

ClockNode* ValueNode::getClock() {
    if(clocks_.size() == 1)
        return clocks_[0];
    
    std::string errMsg = getClassName() + "::getClock : ";
    if(clocks_.size() > 1)
        throw std::logic_error(errMsg + "ValueNodes should only have one clock.");
    if(clocks_.size() == 0)
        throw std::logic_error(errMsg + "All ValueNodes have to have a clock; none found.");
    throw std::logic_error(errMsg + "Should never happen");
    
}

NodeSet combineNodes() {
    return NodeSet{}; 
}

bool hasCompatibleUnits(ValueNode* lhs, ValueNode* rhs) {
    if(lhs->units() == rhs->units())
        return true;
    else if(not lhs->hasUnits() or not rhs->hasUnits())
        return true;
    else
        return false;
}

ValueNode::Units ValueNode::units() const { return units_; }
ValueNode::Units ValueNode::convertUnits(const std::string& unitStr) {
    if(unitStr == "TICKS")              return ValueNode::Units::TICKS;
    else if(unitStr == "INCREASE")      return ValueNode::Units::INCREASE;
    else if(unitStr == "PRICE")         return ValueNode::Units::PRICE;
    else if(unitStr == "SIZE")          return ValueNode::Units::SIZE;
    else if(unitStr == "NONE")          return ValueNode::Units::NONE;
    else throw std::invalid_argument("Unexpected unit string"); 
}

bool ValueNode::hasUnits() const { return units_ != Units::NONE; }; 
bool ValueNode::isPrice() const { return units_ == Units::PRICE; }; 
bool ValueNode::isSize() const { return units_ == Units::SIZE; }
bool ValueNode::isIncrease() const { return units_ == Units::INCREASE; }
bool ValueNode::isTick() const { return units_ == Units::TICKS; }

std::string Theo::symbol() const { return market_data_->symbol(); }
std::string Theo::shortSymbol() const { return getShortSymbol(symbol()); }
std::string Theo::defaultName() const { return getClassName() + shortSymbol(); }
MarketData* Theo::marketData() { return market_data_; }



Theo::Theo(Graph* g, std::string symbol)
    : ValueNode(g, Units::PRICE)
    , market_data_(g->add<RawMarketData>(symbol))
{}
Theo::Theo(Graph* g, MarketData* market_data)
    : ValueNode(g, Units::PRICE),
      market_data_(market_data)
{}
