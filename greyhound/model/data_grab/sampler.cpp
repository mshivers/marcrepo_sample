#include "sampler.h"

NODE_FACTORY_ADD(data_grab::OneInN);
NODE_FACTORY_ADD(data_grab::SubSample);
NODE_FACTORY_ADD(data_grab::LockedBook);
NODE_FACTORY_ADD(data_grab::TheoChange);
NODE_FACTORY_ADD(data_grab::TheoGridChange);


void data_grab::TheoChange::compute() {
    status_ = StatusCode::OK;
    double x = theo_->value();
    ticked_ = not (lowerBound_ <= x and x < upperBound_);
    if(ticked_) {
        upperBound_ = x + dTheo_;
        lowerBound_ = x - dTheo_;
    }
}



void data_grab::TheoGridChange::compute() {
    status_ = StatusCode::OK;
    double x = theo_->value();
    ticked_ = not (lowerBound_ <= x and x < upperBound_);
    if(ticked_) {
        if(x < lowerBound_) {
            lowerBound_ = dLevel_ * ((int)(x / dLevel_));
            upperBound_ = lowerBound_ + 2 * dLevel_;
        } else {
            upperBound_ = dLevel_ * (1 + (int)(x / dLevel_));
            lowerBound_ = upperBound_ - 2*dLevel_;
        }
    }
}

