#include "model/graph.h"
#include "model/marcrepo.h"
#include "model/data_grab/data_grabber.h"
#include "model/config.h"

#include <cmath>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <chrono>

bool Graph::hasCycleUtil(Node* node,
              std::unordered_set<Node*>& visited,
              std::unordered_set<Node*>& recursed) {
    if(!visited.count(node)) {
        visited.emplace(node);
        recursed.emplace(node);

        for(auto child : node->children_)
            if(!visited.count(child)) {
                if(hasCycleUtil(child, visited, recursed))
                    return true;
            }
            else if(recursed.count(child))
                return true;
        recursed.erase(node);
    }
    return false;
}

bool Graph::hasSymmetricEdges() {
    for(auto node : nodes) {
        for(auto parent : node->parents_) {
            if ( std::count(parent->children_.begin(), 
                            parent->children_.end(), node) != 1 )
                throw std::logic_error("parent/child mismatch");
        }
        for(auto child : node->children_) {
            if ( std::count(child->parents_.begin(), 
                            child->parents_.end(), node) != 1 )
                throw std::logic_error("parent/child mismatch");
        }
        for(auto clock : node->clocks_) {
            if ( std::count(clock->callbacks_.begin(), 
                            clock->callbacks_.end(), node) != 1 )
                throw std::logic_error("clock/callback mismatch");
        }
        for(auto callback : node->callbacks_) {
            if ( std::count(callback->clocks_.begin(), 
                            callback->clocks_.end(), node) != 1 )
                throw std::logic_error("clock/callback mismatch");
        }
    }
    return true;
}

Node* Graph::firingNode() {
    if(currentSource_)
        return currentSource_->currentNode();
    else
        return nullptr;
}

void Graph::onInitFinished()
{
    int eventId;
    int i=0;
    while(Config::get("graphVizSaveEvent", "eventId_" + std::to_string(i), eventId))
    {
        if(eventId >= 0)
            graphVizEvent().push_back(eventId);
        ++i;
    }
    if( not valid())
        throw ConfigError("Invalid graph: probably cyclic");
    else
        LOG_INFO() << "onInitFinished: valid graph";
    
}

std::string Graph::graphViz() {
    std::string tab = "    ";
    auto name = [](Node* n) {
        std::stringstream nn;
        nn << '"' << n->getName() << " " << n->id_ << "\\n";
        if(not n->valid())
            nn << "STATUS:" << n->status() << "\\n";

        nn << "nComputed: " << n->nComputed << ". nInvalid: " << n->nTicked - n->nComputed << "\\n";
        auto cn = dynamic_cast<ClockNode*>(n);
        if(cn)
            nn << "nTriggeredCallbacks: " << cn->nTickedTrue << "\\n";
        nn << static_cast<void*>(n);
        nn << '"';
        return nn.str();
    };

    std::stringstream nodesDot;
    for(const auto n: nodes) {
        nodesDot << tab << name(n);
        std::string attributes;
        
        if(dynamic_cast<SourceNode*>(n))
            attributes += "style=\"filled\" ";
        if(dynamic_cast<ClockNode*>(n))
            attributes += "color=\"red\" ";
        if(attributes != "")
            nodesDot << " [ " << attributes << "]";

        nodesDot << "\n";
    }

    std::stringstream rankDot;
    rankDot << "{ rank=min";
    for(const auto n: nodes) {
        if(dynamic_cast<SourceNode*>(n))
            rankDot << "; " << name(n);
    }
    rankDot << "}\n";

    std::stringstream edgesDot;
    for(const auto n: nodes) {
        for(const auto clbk: n->callbacks_)
            edgesDot << tab << name(n) << " -> " << name(clbk) << "\n";
        for(const auto child: n->children_)
            edgesDot << tab << name(n) << " -> " << name(child) << " [style=\"dotted\"]\n";
    }

    std::stringstream graphDot;
    graphDot << "digraph G {\n";

    graphDot << "\n// Nodes: red=ClockNode\n";
    graphDot << nodesDot.str();

    graphDot << "\n// SourceNodes at the top\n";
    graphDot << rankDot.str();
    
    graphDot << "\n// Edges: dotted=Child, not_dotted=Callback\n";
    graphDot << edgesDot.str();

    graphDot << "\nlabelloc=\"t\"\n";
    graphDot << "label=\"red nodes=clocks  solid red=Source  lines=callbacks   dotted=children\"\n";
    
    graphDot << "}\n";
    return graphDot.str();
}

bool Graph::saveGraphViz(std::string fileName) {
    std::string p = exePath().native();
    Path filePath(std::string(p.begin(), p.begin() + p.find_last_of("/")));
    filePath /= fileName;
    std::string fDot = filePath.native() + ".dot";
    std::string fPng = filePath.native() + ".png";
    {
        std::ofstream f(fDot, std::ios::out);
        f << graphViz();
        f.close();
    }
    // This command line puts the disconnected components in a grid.
    // https://stackoverflow.com/questions/8002352/how-to-control-subgraphs-layout-in-dot/8003622#8003622
    std::string cmd = "ccomps -x " + fDot + " | dot | gvpack -array | neato -Tpng -n2 -o " + fPng;
    return system(cmd.c_str());
}


void Graph::nodeAudit(Node* node)
{
    LOG_INFO() << "EventId_ " << eventId_ << ". Audit of: " << node->getName();
    std::string clockMsg;
    auto maybeClock = dynamic_cast<ClockNode*>(node);
    if(maybeClock)
        clockMsg = " nTriggeredCallbacks: " + std::to_string(maybeClock->nTickedTrue) + '.';
        
    LOG_INFO() << "nComputed: " << node->nComputed << '.'
               << " nInvalid: " << node->nTicked - node->nComputed << "."
               << clockMsg
               << '\n';

    std::set<SourceNode*> sources;
    std::set<ClockNode*> nonsource_clocks;
    auto accumParentSources = [&] (Node* n) -> void {
        SourceNode* sn = dynamic_cast<SourceNode*>(n);
        if(sn) {
            sources.insert(sn);
        } else {
            ClockNode* cn = dynamic_cast<ClockNode*>(n);
            if(cn) {
                nonsource_clocks.insert(cn);
            }
        }
    };
    applyToDependencies(node, accumParentSources);
    LOG_INFO() << "\t" << sources.size() << " sources -- nTickedTrue/nTicked:";
    for(auto s: sources)
        LOG_INFO() << "\t\t" << s->getName() << "\t " << s->nTickedTrue << "/" << s->nTicked;

    LOG_INFO() << "\tOf the " << nonsource_clocks.size() << " other clocks, these ticked fewer than twice-- nTickedTrue/nTicked:";
    for(auto s: nonsource_clocks)
        if ((s->nTicked<2) and (not dynamic_cast<OnAny*>(s)))
            LOG_INFO() << "\t\t" << s->getName() << "\t " << s->nTickedTrue << "/" << s->nTicked;

    std::set<Node*> invalids;
    std::set<Node*> invalidsValidParents;
    auto accumInvalids = [&] (Node* n) -> void {
        if(n->status() != Node::StatusCode::OK)
        {
            invalids.insert(n);
            if(n->parentsValid())
                invalidsValidParents.insert(n);
        }
    };
    applyToDependencies(node, accumInvalids);
    LOG_INFO() << "\t" << invalidsValidParents.size() << " invalid dependencies without invalid parents:";
    for(auto inv: invalidsValidParents) {
        LOG_INFO() << "\t\t" << inv->getName() << '\t' << "status: " << inv->status();
    }
    LOG_INFO() << "\t" << invalids.size() << " invalid dependencies:";
    for(auto inv: invalids) {
        LOG_INFO() << "\t\t" << inv->getName() << '\t' << "status: " << inv->status();
    }
    node->audit();
}

void Graph::notifyPreFire(SourceNode* source) {
    #ifndef NDEBUG
    auto it = std::find(graphVizEvent_.begin(), graphVizEvent_.end(), eventId_);
    if(it != graphVizEvent_.end())
        saveGraphViz("graph_" + std::to_string(eventId_));

    if(nodeStatus_.size() != nodes.size()) {
        nodeStatus_.clear();
        for(auto* n: nodes)
            nodeStatus_.push_back(n->status());
    }
    #endif

    currentSource_ = source;
    startFireTime_ = simClock::now();
    wallTStartFire_ = wallClock::now();
    uptime_ = startFireTime_ - startNSec_;
    ++eventId_;

    #ifndef PRODUCTION_BUILD
    auto dt = wallTStartFire_ - wallTEndFire_;
    auto dtNSec = std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count();
    double dtUSec = dtNSec / 1000.0;
    histogram_.update("interEvent", dtUSec);        
    #endif
}


void Graph::notifyPostFire()
{
    #ifndef NDEBUG
    size_t i=0;
    bool hasStatusChange = false;
    std::vector<Node*> newlyInvalid;
    for(auto* n: nodes) {
        if(nodeStatus_.at(i) != n->status()) {
            LOG_INFO() << "NodeStatus change " << nodeStatus_[i] << " -> " << n->status()
                       << " Node=" << n->getName() << " "
                       << " Source=" << currentSource_->getName();
            if(not n->valid()) {
                bool allParentsValid = n->parentsValid();
                bool allParentsInvalid = true;
                for(auto* p:n->parents()) {
                    if(p->status_ != Node::StatusCode::INVALID) {
                        allParentsInvalid = false;
                        break;
                    }
                }
                std::string msg = allParentsValid ? "all valid." : ( allParentsInvalid ? "all invalid." : "various status:");
                LOG_INFO() << "    Status of parents:" << msg;
                if(not allParentsValid and not allParentsInvalid) {
                    for(auto* p:n->parents()) {
                        LOG_INFO() << "        " << p->getName() << ':' << p->status();
                    }
                }
            }

            hasStatusChange = true;
            nodeStatus_[i] = n->status();
        }
        ++i;
    }
    if(hasStatusChange)
        for(auto n: nodesToAudit_)
            nodeAudit(n);
    #endif
    
    #ifndef PRODUCTION_BUILD
    wallTEndFire_ = wallClock::now();
    auto dt = wallTEndFire_ - wallTStartFire_;
    auto dtNSec = std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count();
    double dtUSec = dtNSec / 1000.0;
    auto mds = dynamic_cast<MarketDataSource*>(currentSource_);
    if ( mds != nullptr) {
        // auto order_book = mds->orderBook();
        std::string desc = mds->shortSymbol(); // + (order_book->isTradeEvent()?"Trade":"Quote");
        histogram_.update(desc, dtUSec);
    } else {
        histogram_.update(currentSource_->getName(), dtUSec);
    }

    if(0 == (eventId_ & (eventId_ - 1)) and eventId_ > 1000) // On eventId_ == power of two and > 1000
    {
        for(auto n: nodesToAudit_)
            nodeAudit(n);
    }
    #endif
    currentSource_ = nullptr;
}

void Graph::setDataGrabber(data_grab::DataGrabber* dataGrabber)
{
    addUtilityNode(dataGrabber);
}

void Graph::addUtilityNode(ValueNode* node) {
    assert(node);
    if(std::find(utilityNodes_.begin(), utilityNodes_.end(), node) != utilityNodes_.end())
        throw std::logic_error("Graph::addUtilityNode : Duplicate node inserted into utilityNodes");
    utilityNodes_.push_back(node);
}

void Graph::loadUtilityNodes(JSON const& json)
{
    if(!json.is_array())
        throw ConfigError("JSON object expected for 'utilityNodes'");
    for(auto it = json.begin(); it != json.end(); ++it)
        addUtilityNode(this->deserialize<ValueNode>(it.value()));
}

Strategy* Graph::getStrategy() const
{
    return strategyPtr_;
}

void Graph::setStrategy(Strategy* strategy)
{
    strategyPtr_  = strategy;
}
