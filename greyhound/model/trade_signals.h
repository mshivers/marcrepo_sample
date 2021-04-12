
#pragma once
#include <algorithm> //min & max
#include <cmath>
#include <chrono>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#include <boost/circular_buffer.hpp>
#pragma GCC diagnostic pop

#include "clocks.h"
#include "ema.h"
#include "graph.h"
#include "market_data.h"
#include "serialize.h"
#include "theos.h"
#include "util_nodes.h"

using seconds = std::chrono::seconds;

double approxSigmoid(double x, int C);
double shrinkToZero(double a, double b);

struct SignedVolume : ValueNode {
    std::string symbol() const;
    std::string shortSymbol() const;
    MarketData* marketData();

    MarketData* market_data_;
    SignedVolume(Graph* g, std::string symbol);
    SignedVolume(Graph* g, MarketData* market_data);
};

//TreeSV implements a json parameterization of an impact tree
struct TreeSV : public Theo {
    void compute() override {
        if ( base_theo_->marketData()->isTrade() ) {
            //find leaf node
            int idx = 0;
            do {
                if ( feature_[idx]->value() < threshold_[idx] )
                    idx = left_idx_[idx];
                else
                    idx = right_idx_[idx];
            } while ( idx > 0 );
            
            //calc leaf impulse
            idx = abs(idx);
            double trade_impulse = 0;
            auto trade_size = signed_trade_size_->value();
            for ( size_t i=0; i<stretch_[idx].size(); ++i ) {
                auto stretch = stretch_[idx][i];
                auto coeff = coeff_[idx][i];
                trade_impulse += coeff * approxSigmoid(trade_size, stretch);
            }
            impact_decay_rate_ = decay_[idx];
            impact_theo_wgt_ = 1.0; 
            impact_theo_value_ = base_theo_->heldValue() + trade_impulse;
            value_ = impact_theo_value_;
        } else {
            impact_theo_wgt_ *= impact_decay_rate_;
            value_ = base_theo_->heldValue() + impact_theo_wgt_ * (impact_theo_value_ - base_theo_->heldValue());
        }
        status_ = StatusCode::OK;
    }

    SERIALIZE(TreeSV, base_theo_, feature_, threshold_, left_idx_, right_idx_, stretch_, coeff_, decay_);
    
    Theo* base_theo_;
    std::vector<ValueNode*> feature_;
    std::vector<double> threshold_;
    std::vector<int> left_idx_;
    std::vector<int> right_idx_;
    std::vector<std::vector<int> > stretch_;
    std::vector<std::vector<double> > coeff_;
    std::vector<double> decay_;

    double impact_theo_value_;
    double impact_theo_wgt_{0};
    double impact_decay_rate_{0};
    ValueNode* signed_trade_size_; 

    protected:
    TreeSV(Graph* g, Theo* base_theo, 
              std::vector<ValueNode*> feature,
              std::vector<double> threshold,
              std::vector<int> left_idx,
              std::vector<int> right_idx,
              std::vector<std::vector<int> > stretch,
              std::vector<std::vector<double> > coeff,
              std::vector<double> decay) 
        : Theo(g, base_theo->marketData()),
          base_theo_(base_theo),
          feature_(feature),
          threshold_(threshold),
          left_idx_(left_idx),
          right_idx_(right_idx),
          stretch_(stretch),
          coeff_(coeff),
          decay_(decay) {
        signed_trade_size_ = g->add<SignedTradeSize>(base_theo->marketData()); 
        setParents(base_theo_, feature_, signed_trade_size_); 
        setClock(base_theo_);
    }
};

    
//basic sigmoid trade signed volume that decays on quotes.
struct SigmoidSV : public SignedVolume {
    void compute() override {
        //aproxSigmoid(x)=0.5 when x=1
        //decay old value
        value_ *= decay_factor_;
        if ( signed_trade_size_->ticked() ) {
            //if new impulse is bigger, use that
            double candidate = approxSigmoid(signed_trade_size_->value(), half_impact_size_);
            if ( ( value_ >= 0 and (candidate < 0 or candidate > value_) ) or 
                 ( value_ <= 0 and (candidate > 0 or candidate < value_) ) ) {
                value_ = candidate;
            }
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + market_data_->shortSymbol() + "_"
              + std::to_string((int)half_impact_size_) + "c"
              + std::to_string((int)length_in_ticks_) + "t");
    }

    SERIALIZE(SigmoidSV, market_data_, half_impact_size_, length_in_ticks_);
    
    double half_impact_size_;
    double length_in_ticks_;
    double decay_factor_;
    ValueNode* signed_trade_size_; 
    
    protected:
    SigmoidSV(Graph* g, MarketData* market_data, double half_impact_size, 
        double length_in_ticks)
        : SignedVolume(g, market_data),
          half_impact_size_(half_impact_size), 
          length_in_ticks_(length_in_ticks){
        value_ = 0;
        status_ = StatusCode::OK;
        assert(length_in_ticks >= 1);
        assert(half_impact_size > 0);
        signed_trade_size_ = g->add<SignedTradeSize>(market_data);
        decay_factor_ = (length_in_ticks - 1) / length_in_ticks;
        setParents(signed_trade_size_); 
        setClock(market_data);
    }
};

struct EMSSigmoidSV : public SignedVolume {
    int sign(double x) { return (x>0)?1:((x<0)?-1:0); }

    void compute() override {
        //aproxSigmoid(x)=0.5 when x=half_impact_size
        value_ = approxSigmoid(trade_size_sum_->value(), half_impact_size_);
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        std::string short_sym = market_data_->shortSymbol();
        return (getClassName() +  short_sym + "_" 
              + std::to_string((int)half_impact_size_) + "c"
              + std::to_string((int)length_in_ticks_) + "t");
    }

    SERIALIZE(EMSSigmoidSV, market_data_, half_impact_size_, length_in_ticks_);
    
    double half_impact_size_;
    double length_in_ticks_;
    ValueNode* trade_size_sum_; 
    ValueNode* signed_trade_size_; 
    
    protected:
    EMSSigmoidSV(Graph* g, MarketData* market_data, double half_impact_size, 
        double length_in_ticks)
        : SignedVolume(g, market_data),
          half_impact_size_(half_impact_size), 
          length_in_ticks_(length_in_ticks){
        value_ = 0;
        status_ = StatusCode::OK;
        assert(half_impact_size > 0);
        signed_trade_size_ = g->add<SignedTradeSize>(market_data);
        auto padded_trade_size = g->add<Pad>(signed_trade_size_, market_data, 0);
        trade_size_sum_ = g->add<TickDecayedSum>(padded_trade_size, market_data, 
                                          length_in_ticks_);
        setParents(trade_size_sum_); 
        setClock(market_data);
    }
};

//PersistentSV is a product signed volume type signal where the individual SVs are a 
//fast and slow SV for the same symbol, so a new trade only causes a signal spike if it's
//in the same direction as the longer history of trades.
struct PersistentSV : public SignedVolume {
    void compute() override {
        value_ = shrinkToZero(svslow_->heldValue(), svfast_->heldValue());
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        std::string short_sym = market_data_->shortSymbol();
        return (getClassName() +  short_sym + "_" 
              + std::to_string((int)fast_half_impact_size_) + "c"
              + std::to_string((int)fast_length_in_ticks_) + "t" + "_" 
              + std::to_string((int)slow_half_impact_size_) + "c" 
              + std::to_string((int)slow_length_in_ticks_) + "t");
    }

    SERIALIZE(PersistentSV, market_data_, fast_half_impact_size_, 
        fast_length_in_ticks_, slow_half_impact_size_, slow_length_in_ticks_);

    protected:
    double fast_half_impact_size_;
    int fast_length_in_ticks_;
    double slow_half_impact_size_;
    int slow_length_in_ticks_;
    ValueNode* svfast_;
    ValueNode* svslow_;
    
    PersistentSV(Graph* g, MarketData* market_data,
        double fast_half_impact_size, int fast_length_in_ticks, 
        double slow_half_impact_size, int slow_length_in_ticks)
        : SignedVolume(g, market_data),
          fast_half_impact_size_(fast_half_impact_size), 
          fast_length_in_ticks_(fast_length_in_ticks),
          slow_half_impact_size_(slow_half_impact_size), 
          slow_length_in_ticks_(slow_length_in_ticks) {
        value_ = 0;
        status_ = StatusCode::OK;
        svfast_ = g->add<EMSSigmoidSV>(market_data, fast_half_impact_size, fast_length_in_ticks);
        svslow_ = g->add<EMSSigmoidSV>(market_data, slow_half_impact_size, slow_length_in_ticks);
        setParents(svfast_, svslow_);
        setClock(market_data);
    }
};


//Signal that fires when two sigmoidSVs are firing at the same time and in the same
//direction.  Value is the smallest of the two.  Standard use-case is for the 
//half_impact_sizes to be proportional to duration (or spread weights)
struct ProdSV : public ValueNode {
    void compute() override {
        value_ = shrinkToZero(sv1_->heldValue(), sv2_->heldValue());
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + getShortSymbol(md1_->symbol()) 
              + getShortSymbol(md2_->symbol()) + "_"
              + std::to_string((int)half_impact_size1_) + "c"
              + std::to_string((int)length_in_ticks_) + "t_" 
              + std::to_string((int)half_impact_size2_) + "c"
              + std::to_string((int)length_in_ticks_) + "t");
    }

    SERIALIZE(ProdSV, md1_, half_impact_size1_, md2_,
        half_impact_size2_, length_in_ticks_);

    protected:
    MarketData* md1_;
    double half_impact_size1_;
    MarketData* md2_;
    double half_impact_size2_;
    int length_in_ticks_;
    ValueNode* sv1_;
    ValueNode* sv2_;

    ProdSV(Graph* g, MarketData* md1, double half_impact_size1, 
        MarketData* md2, double half_impact_size2, int length_in_ticks)
        : ValueNode(g, Units::NONE),
          md1_(md1),  
          half_impact_size1_(half_impact_size1), 
          md2_(md2),  
          half_impact_size2_(half_impact_size2), 
          length_in_ticks_(length_in_ticks) {
        value_ = 0;
        status_ = StatusCode::OK;
        sv1_ = g->add<EMSSigmoidSV>(md1, half_impact_size1, length_in_ticks);
        sv2_ = g->add<EMSSigmoidSV>(md2, half_impact_size2, length_in_ticks);
        setParents(sv1_, sv2_);
        setClock(sv1_, sv2_);
    }
};

struct CorrSV : public SignedVolume {
    void compute() override {
        value_ = corr_->heldValue() * ref_sv_->heldValue();
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return ("Corr" + ref_sv_->getName());
    }

    SERIALIZE(CorrSV, base_md_, ref_sv_);

    MarketData* base_md_;
    SignedVolume* ref_sv_;
    ValueNode* corr_;

    protected:
    CorrSV(Graph* g, MarketData* base_md, SignedVolume* ref_sv)
    : SignedVolume(g, base_md), 
      base_md_(base_md),
      ref_sv_(ref_sv){ 
        auto base_midpt_ = g->add<Midpt>(base_md);
        auto ref_midpt_ = g->add<Midpt>(ref_sv->marketData()); 
        corr_ = g->add<EMACorr>(base_midpt_, ref_midpt_, 60*60, seconds{1}, 1.0);
        setParents(ref_sv_, corr_);
        setClock(ref_sv_, corr_);
    }
};
