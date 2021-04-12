import pandas as pd
import numpy as np
import scipy as sp
from scipy import stats
import json
import os
import re
from scipy import optimize as opt
from datetime import datetime
from pathlib import Path
import random, string
import sys
import time
from collections import Counter
sys.path.append('/home/mshivers/marcrepo/python')
        
data_hash = 'BrvC8v'  # signals/states, but only shorter emas for CTs and SVs, full data grab (above failed early in the day)

traded_symbol = "CME:GE%1Q"
short_sym = "GE1Q"
ticksize = 0.005
tt = 'mkp_time_until_BBO_change_10m'

def nodes_by_name(node_list):
    out = dict()
    for node in node_list:
        if 'name' in node.keys():
            out[node['name']] = node
        else:
            out[node['type']] = node
    return out


class FittingData:
    _root_dir = Path('/data/data_grab/')

    def __repr__(self):
        return "FittingData('%s')" % self.path
    
    def __init__(self, path):
        path = path.strip()
        self.path=Path(path)
        if(self.path.exists()):
            self.full_path = self.path
        else:
            self.full_path = self._root_dir / path
        assert(self.full_path.exists())

        self.setup_dir = self.full_path / 'setup'
        self.data_dir = self.full_path / 'data'

        # delay loading the JSON: they could be very big
        self._meta_data = None
        self._config = None
        self._strategy = None
        self._files = None

        self.cols = list(self.meta_data['dtypes'].keys())
        
        self.cov = None
        self.cov_sum = None
        self.cov_row_count = 0
        self._cached = dict()
                  
    @property
    def meta_data(self):
        if self._meta_data is None:
            self._meta_data = self._load_json('meta_data.json')
        return self._meta_data

    @property
    def config(self):
        if self._config is None:
            self._config = self._load_json('config.json')
        return self._config

    @property
    def strategy(self):
        if self._strategy is None:
            self._strategy = self._load_json('DATA_GRAB.json')
        return self._strategy

    @property
    def dates(self):
        return self.config['dates']
        
    @property
    def data_files(self):
        if self._files is None:
            csv_files = sorted(self.data_dir.glob('*.csv'))
            parquet_files = sorted(self.data_dir.glob('*.parquet'))
            if csv_files:
                self._files = [f for f in csv_files if f.name != 'data.csv']
            if parquet_files:
                self._files = parquet_files
        return self._files

    def loadData(self, f):
        try:
            if f.name.endswith('.csv'):
                tmp = pd.read_csv(f)
                if tmp.iloc[-1].isna().any():
                    tmp = tmp.iloc[:-1]
                return tmp
            else:
                return pd.read_parquet(f)
        except:
            print("Opening file {} failed".format(f))
        return [] 

    def _load_json(self, f_name):
        with open(self.setup_dir / f_name) as f:
            return json.load(f)

    def day_dir(self, d):
        day_dir = self.full_path / d
        assert day_dir.exists(), 'Directory %s does not exit' % day_dir
        return day_dir
    
    @property
    def markups(self):
        return [c for c in self.cols if c.startswith('mkp_') and (('Midpt' in c) or ('VWAP' in c))] 

    @property
    def signals(self):
        return list(nodes_by_name(meta['signals']).keys())

    def baseline(self, df):
        return meta['midpt_name']
   
    def getUpdatedCov(self, update_days, clip):
        days = 0
        def cache_fname(f):
            return f.replace('.parquet', '.csv') + ".cov"

        while (days<update_days):
            days += 1
            f = np.random.choice(self.data_files)
            update_cov = None
            if f.name in self._cached:
                update_cov = self._cached[f.name]
            else:
                if os.path.exists(cache_fname(str(f))):
                    update_cov = pd.read_csv(cache_fname(str(f)), index_col=0)
                    self._cached[f.name] = update_cov
                else:
                    fmt = "%H:%M:%S"
                    print(time.strftime(fmt), ": loading data for ", f)
                    df = self.loadData(f)

                    print(time.strftime(fmt), ": data loaded")
                    col_names = self.markups + self.signals + ['const']
                    if len(df):
                        df = self.clipMarkups(df, clip)
                        df['const'] = 1
                        print(time.strftime(fmt), ": Calculating cov")
                        update_cov = df[col_names].T.dot(df[col_names]) 
                        print(time.strftime(fmt), ": cov calculated")
                        self._cached[f.name] = update_cov
                        update_cov.to_csv(cache_fname(str(f)))

            if update_cov is None:
                print("Covariance for {} is None".format(f.name))
            else:
                if self.cov_sum is None:
                    self.cov_sum = update_cov
                    self.cov_row_count = update_cov['const'].loc['const']
                else:
                    self.cov_sum = self.cov_sum + update_cov 
                    self.cov_row_count += update_cov['const'].loc['const']
                self.cov = self.cov_sum / self.cov_row_count
        return self.cov 


    def getRandomDateData(self):
        f = np.random.choice(self.data_files)
        fmt = "%H:%M:%S"
        print(time.strftime(fmt), ": loading data for ", f)
        df = self.loadData(f)
        print(time.strftime(fmt), ": data loaded")
        return df

class NNLS:
    def __init__(self, XtX, price_signals, zero_signals, markup):
        self.price_signals = price_signals 
        self.zero_signals = zero_signals
        self.signals = price_signals + zero_signals
        self.markup = markup
        cols = self.signals+[self.markup]
        self.XtX = XtX.loc[cols, cols]
        self.passive_set = list()
        self.active_set = list()
        self.devnull = list()
        self.sigcount = Counter()
        assert self.markup in self.XtX.columns
        assert all([c in self.XtX.columns for c in self.signals])
 
    def getNextSignal(self):
        if len(self.active_set)==0: return None
        active_error_cov = self.gramian[self.markup].loc[self.active_set] - self.gramian[self.signals].dot(self.beta).loc[self.active_set]
        active_var = pd.Series(np.diag(self.gramian), self.gramian.columns).loc[self.active_set]
        error_var = self.gramian[self.markup].loc[self.markup] - self.gramian[self.signals].loc[self.signals].dot(self.beta).dot(self.beta)
        active_beta = active_error_cov / active_var.apply(np.sqrt) / np.sqrt(error_var)
        if active_error_cov.max() <= 0: 
            print("No more positively-correlated signals in the active set")
            return None

        idx = active_error_cov>0 #1e-6
        val = 0
        if idx.any():
            possible_sigs = list(active_error_cov.loc[idx].index)
            score = active_error_cov.loc[possible_sigs] 
            sig = score.idxmax()
            val = score.loc[sig]
            self.sigcount.update([sig]) 
            if self.sigcount.get(sig)>10:
                self.active_set.remove(sig)
                print("Too many round trips... removing {}".format(sig))
                return self.getNextSignal()
        else:
            sig = active_beta.loc[active_beta>0].idxmin()
            print("Removing {}, cov: {:1.5f}, beta: {:1.6f}".format(sig, active_error_cov.loc[sig], active_beta.loc[sig]))
            self.active_set.remove(sig)
            return self.getNextSignal() 
        print("nextSignal is {}, with score: {}, cov: {}, beta: {}, and sensitivity: {}".format(sig, val, active_error_cov.loc[sig], active_beta.loc[sig], self.sensitivities.loc[sig]))
        print("next Signal variance: {:1.5f}".format(active_var.loc[sig]))
        return sig

    def getPassiveFit(self):
        X = self.gramian[self.passive_set].loc[self.passive_set]
        y = self.gramian[self.markup].loc[self.passive_set]
        coeffs = np.linalg.lstsq(X,y)[0]
        passive_fit = pd.Series(coeffs, self.passive_set)
        return passive_fit

    def updateBeta(self):
        passive_fit = self.getPassiveFit()
        non_const_fit = passive_fit.loc[[c for c in self.passive_set if c!='const']]
        while non_const_fit.min()<=0:
            idx = non_const_fit.index[non_const_fit<=0]
            alpha = (self.beta / (self.beta - passive_fit)).loc[idx].min()
            self.beta = self.beta - alpha * (self.beta - passive_fit.reindex(self.signals).fillna(0))
            remove = self.beta.loc[idx].idxmin()
            print("removing signal {} with passive fit {} and updated beta {}".format(remove, passive_fit.loc[remove], self.beta.loc[remove]))
            self.passive_set.remove(remove)
            #self.devnull.append(remove)
            self.active_set.append(remove)
            passive_fit = self.getPassiveFit()
            non_const_fit = passive_fit.loc[[c for c in self.passive_set if c!='const']]
                
        print(passive_fit.sort_index())
        self.beta = passive_fit.reindex(self.signals).fillna(0)

    def calcSignalSensitivities(self):
        remaining_signals = self.signals.copy()
        sensitivities = pd.Series(np.zeros(len(self.signals)), self.signals)
        regularized_gramian = self.gramian + 1e-4 * np.eye(self.gramian.shape[0])
        if 1:
            inv = np.linalg.inv(regularized_gramian[remaining_signals].loc[remaining_signals])
            sensitivities = pd.Series(np.diag(inv), remaining_signals)
        else:
            while len(remaining_signals)>0:
                inv = np.linalg.inv(regularized_gramian[remaining_signals].loc[remaining_signals])
                remaining_sensitivities = pd.Series(np.diag(inv), remaining_signals)
                m = remaining_sensitivities.idxmax()
                sensitivities.loc[m] = remaining_sensitivities.loc[m]
                remaining_signals.remove(m)
        sensitivities = sensitivities.apply(np.sqrt)
        self.sensitivities = sensitivities / sensitivities.max()

    def calcRegularizationMatrix(self):
        reg_matrix = np.zeros(self.gramian.shape)
        #center const coeff around 1
        self.gramian['const'] *= 1000
        self.gramian.loc['const'] *= 1000
        self.gramian[self.markup] += self.gramian['const']
        self.gramian.loc[self.markup,:] += self.gramian.loc['const']

         #sum of price-based set to 1
        N =10* np.sqrt(self.gramian.loc[self.markup, self.markup])
        vec = pd.Series(np.zeros(self.gramian.shape[0]), self.gramian.columns)
        vec.loc[self.price_signals] = N
        vec.loc[self.markup] = N
        self.gramian += np.outer(vec,vec)
       
        
        C=1000
        for s in self.signals:
            vec.loc[:] = 0
            vec.loc[self.price_signals] = C
            vec.loc[s] += self.sensitivities.loc[s] / C
            self.gramian += 1e-6 * np.outer(vec, vec)
       
       
         
        self.reg_matrix = reg_matrix

    def fit(self):
        self.passive_set = ['const']
        self.active_set = [s for s in self.signals if s not in self.passive_set]
        self.gramian = self.XtX.copy()

      
        self.calcSignalSensitivities()
        self.calcRegularizationMatrix()
        self.beta = pd.Series(np.zeros(len(self.signals)), self.signals)
        error = self.XtX[self.markup] 
         
        count = 0
        sig = self.getNextSignal()
        while sig != None:
            self.active_set.remove(sig)
            self.passive_set.append(sig)
            self.updateBeta()
            sig = self.getNextSignal()
            count += 1
            print("Count: {}".format(count))



if 'fdata' not in locals().keys():
    fitting_data = FittingData(data_hash)
    #fdata = fitting_data.getRandomDateData()
    fdata = fitting_data.getUpdatedCov(30, 2*ticksize)
    meta = fitting_data.meta_data 
markup_name = np.random.choice([c for c in fitting_data.markups if '1s' not in c and 'VWAP' not in c])
print("Markup: {}".format(markup_name))


signal_dict = nodes_by_name(meta['signals'])
price_signals = nodes_by_name(meta['price_signals'])  
info_dict = nodes_by_name(meta['info_columns'])
signal_names = [s for s in list(signal_dict.keys())] + ['const']
price_signals = [s for s in list(price_signals.keys())] 
zero_signals = [s for s in signal_names if s not in price_signals] 

model = NNLS(fdata, price_signals, zero_signals, markup_name)
model.fit()
beta = model.beta.loc[model.beta.abs()>=1e-7].sort_index() 
print(beta)
pb = [s for s in beta.index if s in price_signals]
print("{} Price-based signals sum: {:1.6f}".format(len(pb), beta.loc[pb].sum()))

