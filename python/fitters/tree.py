import multiprocessing
import pandas as pd
import numpy as np
import scipy as sp
from scipy import optimize as opt
from matplotlib import pyplot as plt
from collections import Counter, defaultdict
from sklearn.model_selection import KFold

#TODO(mshivers): modify this so that the tree will tick on all relevant events, not just on the traded_symbol's trades.
# Try just adding a very fast ems of traded_symbol's signed trade size to the state variables, then getting rid of the
# impact curves? Or adding a fit of simple theos/comptheos in each leaf?

#TODO(mshivers): build a tree to estimate the impact of add and cancel orders.  Build a theo that approximates the tree.

#for plotting in ticksize units... change 
ticksize = 1/128.0
  
def effective_size(data):
    return len(data['Minute'].unique())

def SigmoidSV(x, c):
    return ( x / (c + abs(x) ))

def SigmoidGenerator(stretch):
    return lambda x:SigmoidSV(x, stretch)


class Model:
    columns = ['SignedTradeSize', 'Markup']
    def __init__(self, data):
        self.data = data[self.columns]
        self.coeffs = None
        self.sse = None 
        self.train_sse = None
        self.test_sse = None
        self.test_rmse = None
        self.test_err = None

    def __call__(self, x):
        return self.apply(x)
    
    def __repr__(self):
        if all((self.coeffs)==0):
            names = ["(SV(0), 0)"]
        else:
            names = list()
            for item in zip(self.C_grid, self.coeffs/ticksize):
                C, coeff = item
                if coeff > 0:
                    names.append("(SV({}), {:1.4f})".format(C, coeff))
        names.append("RMSE Edge: {:1.0f}".format(1000*self.test_rmse/ticksize))
        return "\n".join(names) 

    def sigmoid_stretches(self):
        if all((self.coeffs)==0):
            params = [0]
        else:
            params = list()
            for item in zip(self.C_grid, self.coeffs):
                C, coeff = item
                if coeff > 0:
                    params.append(int(C)) #cast to int since JSON won't serialize np.int64
        return params 

    def sigmoid_coeffs(self):
        if all((self.coeffs)==0):
            params = [0]
        else:
            params = list()
            for item in zip(self.C_grid, self.coeffs):
                C, coeff = item
                if coeff > 0:
                    params.append(coeff) 
        return params 

    def serialize(self):
        if all((self.coeffs)==0):
            params = [(0,0)]
        else:
            params = list()
            for item in zip(self.C_grid, self.coeffs):
                C, coeff = item
                if coeff > 0:
                    params.append((int(C), coeff)) #cast to int since JSON won't serialize np.int64
        return params 

    def SV_values(self, X):
        if isinstance(X, (pd.Series, pd.DataFrame)):
            X = X.values
        if isinstance(X, np.ndarray) and len(X.shape)==1:
            X = X.reshape(-1,1)
        return (X / (self.C_grid + abs(X)))

    def apply(self, X):
        return self.SV_values(X).dot(self.coeffs)
   
    # Note this is the error on a 2-sided impact curve, not just the positive side
    # i.e. SignedTradeSize -> Markup, not TradeSize -> SignedMarkup
    def error(self, X, y):
        return y - self.apply(X) 

    def calc_sse(self, X, y):
        err = self.error(X, y)
        return (err*err).sum()

    def fit(self, cv=True):
        trade_size = self.data['SignedTradeSize'].abs()
        signed_markup = np.sign(self.data['SignedTradeSize']) * self.data['Markup']
        max_val = np.percentile(trade_size, 95)
        exp_range = np.exp(np.arange(0, np.log(max_val), 0.2))
        self.C_grid = np.unique([0,1]+[int(i) for i in exp_range])

        if len(trade_size.unique()) > 2:    #groupby fit
            grp = signed_markup.groupby(trade_size)
            counts = grp.count().values
            weights = np.sqrt(counts)
            mean_signed_markups = grp.mean()
            group_trade_sizes = np.array(mean_signed_markups.index)
            X = self.SV_values(group_trade_sizes) * weights.reshape(-1,1)
            y = np.squeeze(mean_signed_markups.values * weights)
            self.coeffs = opt.nnls(X, y)[0]
        else:                               #regular data fit
            X = self.SV_values(trade_size) 
            y = signed_markup
            self.coeffs = opt.nnls(X, y)[0]
        if cv:
            self.cross_validate()
        return self

    def cross_validate(self, n_splits=2):
        self.test_err = np.zeros(len(self.data))
        train_sse = 0
        kf = KFold(n_splits=n_splits)
        for train, test in kf.split(self.data):
            train_data = self.data.iloc[train]
            train_X = train_data['SignedTradeSize']
            train_y = train_data['Markup']
            test_data = self.data.iloc[test]
            test_X = test_data['SignedTradeSize']
            test_y = test_data['Markup']
            model = Model(train_data)
            model.fit(cv=False)
            self.test_err[test] = model.error(test_X, test_y) 
            train_sse += model.calc_sse(train_X, train_y)
        train_sse /= (n_splits - 1)
        self.train_sse = train_sse 
        self.test_sse = (self.test_err**2).sum()
        self.test_rmse = np.sqrt(self.test_sse / len(self.test_err))

class Split:
    def __init__(self, node, feature, threshold):
        self.node = node
        self.feature = feature
        self.threshold = threshold 
        self.left_index = self.calc_left_index() 
        self.right_index = self.calc_right_index() 
        self.left_model = None
        self.right_model = None
        self.left_sse_gain = 0
        self.right_sse_gain = 0 
        self.train_sse_gain= 0 
        self.test_sse_gain= 0 
        self.tick_gain = 0
        self.left_tick_gain = 0
        self.right_tick_gain = 0
        self.p_value = None
        self.fit_child_models()

    def __lt__(self, other):
        return self.train_sse_gain < other.train_sse_gain

    def __repr__(self):
        fstr = "Node: {}\nFeature: {}\nThreshold: {}\nTest Tick Gain: {}\np-value: {}"
        return fstr.format(self.node, self.feature, self.threshold, self.tick_gain, self.significance()) 

    def calc_left_index(self):
        return self.node.data[self.feature] < self.threshold

    def calc_right_index(self):
        return self.node.data[self.feature] >= self.threshold

    def train_score(self):
        return self.train_sse_gain

    def test_score(self):
        return self.test_sse_gain

    def fit_child_models(self):
        left_esize = effective_size(self.node.data[['Minute']].loc[self.left_index]) 
        right_esize = effective_size(self.node.data[['Minute']].loc[self.right_index]) 
        min_esize = 100  #test larger leaf nodes
        if left_esize > min_esize and right_esize > min_esize:
            #Fit left model
            self.left_model = Model(self.node.data[Model.columns].loc[self.left_index]).fit()
            left_parent_test_sse = (self.node.model.test_err[self.left_index]**2).sum()
            self.left_sse_test_gain = left_parent_test_sse - self.left_model.test_sse
            lsize = self.left_index.sum()
            self.left_tick_gain = np.sign(self.left_sse_test_gain) * np.sqrt(abs(self.left_sse_test_gain) / lsize) / ticksize

            #Fit right model
            self.right_model = Model(self.node.data[Model.columns].loc[self.right_index]).fit()
            right_parent_test_sse = (self.node.model.test_err[self.right_index]**2).sum()
            self.right_sse_test_gain = right_parent_test_sse - self.right_model.test_sse
            rsize = self.right_index.sum()
            self.right_tick_gain = np.sign(self.right_sse_test_gain) * np.sqrt(abs(self.right_sse_test_gain) / rsize) / ticksize

            #Calc goodness-of-fit stats
            self.train_sse_gain = self.node.model.train_sse - self.left_model.train_sse - self.right_model.train_sse 
            self.test_sse_gain = self.node.model.test_sse - self.left_model.test_sse - self.right_model.test_sse
            self.tick_gain = np.sign(self.test_sse_gain) * np.sqrt(abs(self.test_sse_gain) / (lsize + rsize)) / ticksize
            self.calc_significance()
        else:
            self.p_value = 1

    #returns whether the split is statistically better than the parent
    def calc_significance(self):
        if self.left_model is None or self.right_model is None:
            self.p_value = 1
        else:
            #paired t-test for reduction in squared error
            split_test_err = np.zeros(self.node.size())
            split_test_err[self.left_index] = self.left_model.test_err
            split_test_err[self.right_index] = self.right_model.test_err
            absolute_err_reduction = abs(self.node.model.test_err) - abs(split_test_err)            
            stderr = absolute_err_reduction.std() / np.sqrt(len(absolute_err_reduction))
            mean_reduction = absolute_err_reduction.mean() 
            if stderr > 0:
                self.t = mean_reduction / stderr
            else:
                self.t = np.inf if mean_reduction > 0 else -np.inf

            min_sqr_reduction = (self.node.tree.min_tick_gain * ticksize)**2
            sqr_err_reduction = np.power(self.node.model.test_err,2) - np.power(split_test_err, 2)            
            self.t = (sqr_err_reduction.mean()-min_sqr_reduction) / (sqr_err_reduction.std()/np.sqrt(len(sqr_err_reduction)))
            self.p_value = 1 - sp.stats.t.cdf(self.t, len(split_test_err))

    def significance(self):
        if self.p_value is None:
            self.calc_significance()
        return self.p_value

    def threshold_repr(self):
        if self.node.data[self.feature].dtype == np.dtype('int64'):
            return str(int(self.threshold))
        else:
            return "{:1.5f}".format(self.threshold)
 
    def left_name(self):
        split_name = '{}<{}'.format(self.feature, self.threshold_repr())
        name = "{}|{}".format(self.node, split_name)
        return name

    def right_name(self):
        split_name = '{}>={}'.format(self.feature, self.threshold_repr())
        name = "{}|{}".format(self.node, split_name)
        return name


#TODO(mshivers): move the logic determining whether a node can remove into the Node code, out of Tree.
class Node:
    def __init__(self, tree, data, parent, model):
        self.tree = tree 
        self.data = data
        self.parent = parent 
        self.model = model 
        self.split = None
        self.best_split = None
        self.node_id = None
        self.node_size = None
        self.effective_node_size = None
        self.decay_length = None

    def is_left_node(self):
        return self == self.parent.left_node

    def is_right_node(self):
        return self == self.parent.right_node

    def __repr__(self):
        name = "Unknown"
        if self.parent is None:
            name="Root"
        else:
            if self.is_left_node():
                name = self.parent.split.left_name()
            elif self.is_right_node():
                name = self.parent.split.right_name()
        return name

    def print_leaf(self):
        if self.is_leaf():
            if self.best_split is not None:
                print("{}, {}, {}: {:1.5f}, {}, {}".format(self.node_id, self.size(), 
                                      self.effective_size(),
                                      self.best_split.tick_gain,
                                      self.best_split.feature,
                                      self.best_split.threshold))
            else:
                print("{}, {}, {}: No split possible".format(self.node_id, self.size(), self.effective_size()))
 
    def print_leaves(self):
        self.print_leaf()
        if self.has_split():
            self.left_node.print_leaves()
            self.right_node.print_leaves()

    def depth(self):
        if self.parent is None:
            return 0
        else:
            return self.parent.depth() + 1

    def size(self):
        if self.node_size is None:
            self.node_size = len(self.data)
        return self.node_size

    #This returns the number of instances in the node with unique minute-level timestamps, so 
    #observations nearby (milliseconds) will only count as a single observation
    def effective_size(self):
        if self.effective_node_size is None:
            self.effective_node_size = effective_size(self.data)
        return self.effective_node_size

    def has_split(self):
        return self.split is not None

    def is_leaf(self):
        return self.split == None

    def get_children(self):
        children = [self]
        if self.has_split():
            children = children + self.split.left_node.get_children() + self.split.right_node.get_children()
        return children
         
    def get_leaves(self):
        if self.is_leaf():
            return [self]
        else:
            return self.split.left_node.get_leaves() + self.split.right_node.get_leaves()
   
    def get_best_split(self):
        print("Finding best split for:")
        print("Node: {}".format(self))
        print("Size: {}".format(self.size()))
        print("Effective Size: {}".format(self.effective_size()))
   
        max_test_gain = 0
        best_feature_splits = list()
        max_half_split_score = 0
        for feature in self.tree.features:
            feature_values = self.data[feature].values
            half_threshold = np.percentile(feature_values, 50)
            half_split = Split(self, feature, half_threshold)
            max_half_split_score = max(max_half_split_score, half_split.test_sse_gain)
            splits = [half_split]
            if half_split.test_sse_gain > max_half_split_score/2 or feature=='TradeSize':
                pctls = [10, 20,30,40,60,70,80, 90]
                thresholds = np.unique(np.percentile(feature_values, pctls))
                splits.extend([Split(self, feature, threshold) for threshold in thresholds])
            max_split = max(splits)

            best_feature_splits.append(max_split)
            desc = ''
            if max_split.significance() < self.tree.significance / len(self.tree.features):
                desc = "*"
                if (max_split.test_sse_gain > max_test_gain and 
                    max_split.left_sse_test_gain > 0 and 
                    max_split.right_sse_test_gain > 0):
                    max_test_gain = max_split.test_sse_gain
                    best_split = max_split
                    desc = desc + " <=="

            threshold_pctl = int(100*(feature_values < max_split.threshold).mean())
            fstr = "{:>32}, {: 5d}, {:>12}, {: 0.5f}, {: 0.5f}, {: 0.5f}, {: 0.5f} {}"
            print(fstr.format(max_split.feature, threshold_pctl, max_split.threshold_repr(),
                              max_split.train_sse_gain, max_split.test_sse_gain, max_split.tick_gain, 
                              max_split.significance(), desc))

        #do a multiple selection test on the candidate splits and choose the significant one with max test_sse_gain
        best_split = None
        splits = sorted(best_feature_splits, key=lambda x: x.test_sse_gain)
        n = len(splits)
        if n > 0:
            print("\n")
            for split in splits:
                if split.significance() < self.tree.significance/n:
                    print("{:>32} {:0.3f} {}".format(split.feature, split.tick_gain, "SIGNIFICANT!"))
                    best_split = split
                #else:
                #    print("{:>32} {:0.3f} {}".format(split.feature, split.tick_gain, "doesn't help..."))
            
        if best_split is not None:
            print("\nBest split found:")
            print(best_split)

            print("\nParent Model:")
            print(self.model)

            print("\nLeft Node ({})".format(best_split.left_index.sum()))
            print("Tick Gain: {}".format(best_split.left_tick_gain))
            print("Model:")
            print(best_split.left_model)

            print("\nRight Node ({})".format(best_split.right_index.sum()))
            print("Tick Gain: {}".format(best_split.right_tick_gain))
            print("Model:")
            print(best_split.right_model)
            self.best_split = best_split
        else:
            print("No more splits possible")
            self.best_split = None
        print('\n')

    def divide(self):
        self.split = self.best_split
        self.left_node = Node(self.tree, self.data.loc[self.split.left_index], parent=self, model=self.split.left_model)
        self.right_node = Node(self.tree, self.data.loc[self.split.right_index], parent=self, model=self.split.right_model)
        self.tree.insert_node(self.left_node)
        self.tree.insert_node(self.right_node)

class Tree:
    def __init__(self, data, features, base_theo, markup, significance=0.05, 
                 min_tick_gain=0.01, trading_fee=0.24, notional=200000):
        for feature in ['t'] + features:
            assert feature in data.columns
        assert 'QuotesSinceLastTrade' in data.columns
        assert 'SignedTradeSize' in data.columns
        self.data = data.copy()  #in case a view was passed in...
        self.features = features
        self.base_theo = base_theo
        self.markup = markup
        self.trading_fee = trading_fee
        self.notional = notional
        self.significance = significance
        self.min_tick_gain = min_tick_gain #percent of a tick significant reduction in mrse required to split a node
        self.nodes = list()  
        self.rem_allowed = None
 
    def size(self):
        return len(self.nodes)

    def insert_node(self, node):
        node.get_best_split()
        node.node_id = len(self.nodes)  #position in nodes list
        self.nodes.append(node)

    def print_leaves(self):
        if len(self.nodes) > 0:
            self.nodes[0].print_leaves()

    def print_splits(self):
        for s in self.get_splits():
            print(s)
            print("\n")

    def score(self):
        score = 0
        for node in self.nodes:
            if not node.is_leaf():
                score += node.split.test_score()
        return score

    def tick_gain(self):
        score = self.score()
        return np.sign(score) * np.sqrt(score / self.data.isTrade.sum()) / ticksize

    def get_splits(self):
        splits = [n.split for n in self.nodes if n.has_split()]
        splits = sorted(splits, key=lambda x:x.score(), reverse=True)
        return splits

    def split_nodes(self):
        return [n for n in self.nodes if n.has_split()]

    def leaf_nodes(self):
        return [n for n in self.nodes if n.is_leaf()]

    def find_node_to_split(self):
        candidates = [n for n in self.leaf_nodes() if n.best_split is not None]
        candidates = sorted(candidates, key=lambda x:x.best_split.test_score(), reverse=True)
        if candidates:
            return candidates[0]
        else:
            print("No more splits possible\n")
            return None

    def split_features(self):
        features = list()
        for node in self.nodes:
            if not node.is_leaf():
                if node.split.feature not in features:
                    features.append(node.split.feature)
        return features

    def feature_scores(self):
        counts = Counter()
        scores = defaultdict(lambda :0)
        for node in self.nodes:
            if not node.is_leaf():
                feature = node.split.feature
                counts.update([feature])
                scores[feature] += node.split.test_score()
        results = list()
        for f, c in counts.most_common():
            results.append((f, c, scores[f]))
        results = sorted(results, key=lambda x:x[2], reverse=True)
        print("Feature Scores:")
        for r in results:
            print(r)
        print("\n")
        return results

    def add_split(self, node):
        if node:
            print("Splitting node {} {} on {}".format(node.node_id, node, node.best_split.feature))
            print("Train SSE Gain: {}".format(node.best_split.train_score()))
            print("Test SSE Gain: {}".format(node.best_split.test_score()))
            print("Tick Gain: {}".format(node.best_split.tick_gain))
            print("Split p-value: {}\n".format(node.best_split.significance()))
            node.divide()
            self.feature_scores()

        print("New Tree Score: {}".format(self.score()))
        print("New Tree Tick Gain: {}".format(self.tick_gain()))
        print("Num Leaf Nodes: {}".format(len(self.leaf_nodes())))
        print("Num Terminal Nodes: {}".format(len([n for n in self.leaf_nodes() if n.best_split is None])))
        terminal_pct = sum([n.size() for n in self.leaf_nodes() if n.best_split is None]) / self.nodes[0].size()
        print("Data Pct Terminal Nodes: {}".format(terminal_pct))
        print("Current Unsplit Leaf Nodes:")
        for n in sorted(self.nodes, key=lambda x: x.size(), reverse=True):
            if n.best_split is not None:
                n.print_leaf()
        print("\n")
  
    def build_tree(self):
        if self.size() == 0:
            treedata = self.data.loc[self.data.isTrade==1].copy()
            treedata['Minute'] = treedata['t'].apply(lambda x:str(x)[:16])
            treedata['Markup'] = treedata[self.markup] - treedata[self.base_theo]
            treedata['SignedMarkup'] = np.sign(treedata['SignedTradeSize']) * treedata['Markup']
            model = Model(treedata)
            model.fit()
            model.cross_validate()
            print("Root Model:")
            print(model)
            self.insert_node(Node(tree=self, data=treedata, parent=None, model=model))
        node = self.find_node_to_split()
        while (node):
            self.add_split(node)
            node = self.find_node_to_split()
        print("All Nodes:")
        for n in sorted(self.nodes, key=lambda x: x.size(), reverse=True):
            n.print_leaf()

    def append_forecast_to_data(self):
        self.data['node_id'] = np.nan
        self.data['impulse'] = np.nan
        self.data['forecast'] = np.nan
        self.data['target'] = np.nan
        dfs = list()
        for node in self.leaf_nodes():
           df = pd.DataFrame(data=node.model(node.data.SignedTradeSize), index=node.data.index, columns=['impulse'])
           df['node_id'] = node.node_id
           dfs.append(df)
        fcast = pd.concat(dfs)
        fcast.sort_index(inplace=True)
        self.data.loc[fcast.index, ['impulse', 'node_id']] = fcast
        self.data.loc[fcast.index, 'target'] = self.data.loc[fcast.index, self.markup]
        self.data.loc[fcast.index, 'forecast'] = self.data.loc[fcast.index, self.base_theo] + fcast['impulse']
        self.data.forecast.fillna(method='ffill', inplace=True)
        self.data.node_id.fillna(method='ffill', inplace=True)
        self.data.target.fillna(method='ffill', inplace=True)

    def fit_node_decays(self, plot=False):
        if 'forecast' not in self.data.columns:
            self.append_forecast_to_data()

        def decay_sse(ndata, node, length):
            alpha = np.power((length-1.0)/length, ndata.QuotesSinceLastTrade)
            estimate = alpha * ndata.forecast + (1-alpha) * ndata[self.base_theo]
            sse = ((estimate-ndata.target)**2).sum()
            return sse

        grp = self.data.groupby('node_id')
        for node in self.leaf_nodes():
            fit_cols = ['QuotesSinceLastTrade', 'forecast', 'target', self.base_theo]
            ndata  = grp.get_group(node.node_id)[fit_cols]
            ndata = ndata.loc[ndata.QuotesSinceLastTrade<500]
            
            decay_lengths =  np.unique(np.logspace(2,8, num=20, base=2, dtype=int))
            decays = [ (decay_sse(ndata, node, x), x) for x in decay_lengths] 
            node.decay_length = min(decays)[1]
            print(node.node_id, node.size(), node.decay_length)

            if plot==True:
                qgrp = ndata.groupby('QuotesSinceLastTrade')
                out = list()
                for k in list(qgrp.groups.keys())[:999]:
                    qdata = qgrp.get_group(k)
                    X = qdata[[self.base_theo, 'forecast']].values
                    y = qdata['target'].values
                    if len(y) > 20:
                        out.append((k, *(opt.nnls(X,y)[0])))
                plt.figure()
                x_vals = [a[0] for a in out]
                decay_rate = (node.decay_length - 1.0) / node.decay_length
                plt.plot(x_vals,[a[2] for a in out])
                plt.plot(x_vals,[(decay_rate)**q for q in x_vals])
                plt.title("Node: {}; len: {}\n {}".format(node.node_id, node.decay_length, node))
                plt.show(block=False)
                print("\n\nNode {}:\n {}".format(node.node_id, node))
                print("Model {}".format(node.model))
                print("length: {}".format(node.decay_length))

    #TODO(mshivers): calc optimal in-sample removal edges 
    def determine_removal_leaves(self):
        if 'forecast' not in self.data.columns:
            self.append_forecast_to_data()

        trades = self.data.loc[self.data.isTrade==1].copy()
        trades['RemovalMarkup'] =  np.sign(trades['SignedTradeSize']) * (trades[self.markup] - trades['AvgTradePrice']) 
        trades['RemovalProfit'] = trades['RemovalMarkup'] * self.notional / 100 - self.trading_fee 

        trades['MinEdgeToRemove'] = np.sign(trades['SignedTradeSize']) * (trades['forecast'] - trades['AvgTradePrice'])
        possible_trades = trades.loc[(trades['time_to_tick']>20000) & (trades['MinEdgeToRemove']>0)].copy()
        possible_trades = possible_trades.sort_values('MinEdgeToRemove', ascending=False)
        possible_trades['leaf_profit_to_edge'] = possible_trades.groupby('node_id')['RemovalProfit'].cumsum() 

        tmp = possible_trades.loc[possible_trades.groupby('node_id')['leaf_profit_to_edge'].idxmax()][['node_id',
        'MinEdgeToRemove', 'leaf_profit_to_edge']]
        tmp = tmp.set_index('node_id')
        tmp['obs_count'] = possible_trades.groupby('node_id')['t'].count()
        tmp['num_trades'] = [(possible_trades.loc[possible_trades.node_id==i]['MinEdgeToRemove']>=tmp.loc[i].MinEdgeToRemove).sum() 
                             for i in tmp.index]

        self.rem_allowed = [False] * len(self.leaf_nodes())
        for node_id, row in tmp.iterrows():
            if row.leaf_profit_to_edge>0:
                leaf_index = self.leaf_nodes().index(self.nodes[int(node_id)])
                self.rem_allowed[leaf_index] = True
        self.leaf_summary = tmp 

    #checks to make sure all required parameters have been estimated before serialization
    def valid(self):
        for node in self.nodes:
            if node.node_id is None or self.nodes[node.node_id] != node:
                print("Node IDs are inconsistent with node vector ordering")
                return False
            if node.is_leaf():
               if node.model.coeffs is None:
                   print("Node models are missing")
                   return False
               if node.decay_length is None:
                   print("Node decays are missing")
                   return False
        return True

    def serialize(self, signals):
        split_nodes = self.split_nodes()
        leaf_nodes = self.leaf_nodes()

        split_features = [signals[node.split.feature] for node in split_nodes]
        split_thresholds = [node.split.threshold for node in split_nodes]

        left_index = list()
        right_index = list()
        for node in split_nodes:
            if node.left_node in split_nodes:
                left_index.append(split_nodes.index(node.left_node))
            else:
                left_index.append(-leaf_nodes.index(node.left_node))

            if node.right_node in split_nodes:
                right_index.append(split_nodes.index(node.right_node))
            else:
                right_index.append(-leaf_nodes.index(node.right_node))

        sigmoid_stretch = list()
        sigmoid_coeff = list()
        decay_rate = list()
        rmse = list()
        for node in leaf_nodes:
            sigmoid_stretch.append(node.model.sigmoid_stretches())
            sigmoid_coeff.append(node.model.sigmoid_coeffs())
            decay_rate.append((node.decay_length-1.0)/node.decay_length)
            rmse.append(node.model.test_rmse)

        if self.rem_allowed is None:
            self.determine_removal_leaves()

        params = dict()
        params['base_theo_'] = signals[self.base_theo]
        params['feature_'] = split_features
        params['threshold_'] = split_thresholds
        params['left_idx_'] = left_index
        params['right_idx_'] = right_index
        params['stretch_'] = sigmoid_stretch 
        params['coeff_'] = sigmoid_coeff
        params['decay_'] = decay_rate
        params['rmse_'] = rmse
        params['rem_allowed_'] = self.rem_allowed
        params['type'] = 'TreeSVDynamicEdge2'

        #TODO(mshivers): add fields for prob market ticks both in the thicker and thinner direction in 1 second
        # (need to update tree data grab to get 1-second midpt markup)
        return params

    def display(self):
        displayed_nodes = list()
        data=list()
        split_count = 1
        sse_gain=0
        for node in self.nodes:
            if node.split is not None:
                if node not in displayed_nodes:
                    sse_gain += node.split.test_sse_gain
                    data.append((split_count, sse_gain))
                    print("Split {}:".format(split_count))
                    print("SSE Gain: {}".format(sse_gain))
                    print("Node Size: {}".format(node.size()))
                    print(node.split, '\n')
                    if node.left_node.is_leaf():
                        print("Left Model (leaf ({})):".format(node.left_node.size()))
                    else:
                        print("Left Model ({}):".format(node.left_node.size()))
                    print(node.left_node.model)
                    if node.right_node.is_leaf():
                        print("Right Model (leaf ({})):".format(node.right_node.size()))
                    else:
                        print("Right Model ({}):".format(node.right_node.size()))
                    print(node.right_node.model,'\n')
                    displayed_nodes.append(node)
                    split_count+=1
        plt.plot([d[0] for d in data], [d[1] for d in data])
        plt.show(block=False)

