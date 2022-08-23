#include "storm-pars/analysis/ReachabilityOrderExtenderMdp.h"
#include "storm-pars/api/region.h"

namespace storm {
    namespace analysis {

        template<typename ValueType, typename ConstantType>
        ReachabilityOrderExtenderMdp<ValueType, ConstantType>::ReachabilityOrderExtenderMdp(std::shared_ptr<models::sparse::Model<ValueType>> model, std::shared_ptr<logic::Formula const> formula, bool prMax) : ReachabilityOrderExtender<ValueType, ConstantType>(model, formula) {
            if (formula->isProbabilityOperatorFormula()) {
                this->prMax = storm::solver::maximize(formula->asProbabilityOperatorFormula().getOptimalityType());
            } else {
                assert (false);
            }
        }

        template<typename ValueType, typename ConstantType>
        ReachabilityOrderExtenderMdp<ValueType, ConstantType>::ReachabilityOrderExtenderMdp(storm::storage::BitVector& topStates,  storm::storage::BitVector& bottomStates, storm::storage::SparseMatrix<ValueType> matrix, bool prMax) : ReachabilityOrderExtender<ValueType, ConstantType>(topStates, bottomStates, matrix) {
            this->prMax = prMax;
        }

        template<typename ValueType, typename ConstantType>
        bool ReachabilityOrderExtenderMdp<ValueType, ConstantType>::findBestAction(std::shared_ptr<Order> order, storage::ParameterRegion<ValueType>& region, uint_fast64_t state) {
            // Finding the best action for the current state
            STORM_LOG_INFO("Looking for best action for state " << state << std::endl);
            if (order->isActionSetAtState(state)) {
                STORM_LOG_INFO("Best action for state " << state << " is already set." << std::endl);
                return true;
            }
            if (this->stateMap[state].size() == 1){
                // if we only have one possible action, we already know which one we take.
                STORM_LOG_INFO("   Only one Action available, take it." << std::endl);
                order->addToMdpScheduler(state, 0);
                return true;

            }
            if (order->isTopState(state)) {
                // in this case the state should be absorbing so we just take action 0
                STORM_LOG_INFO("   State is top state, thus absorbing. Take action 0." << std::endl);
                order->addToMdpScheduler(state, 0);
                return true;
            }
            if (order->isBottomState(state)) {
                // in this case the state should be absorbing so we just take action 0
                STORM_LOG_INFO("   State is bottom state, thus absorbing. Take action 0." << std::endl);
                order->addToMdpScheduler(state, 0);
                return true;
            }

            // note that succs in this function mean potential succs
            uint64_t bestAct = this->matrix.getRowCount();
            auto successors = this->getSuccessors(state, order);
            auto orderedSuccs = order->sortStates(successors.second);
            if (orderedSuccs.back() == this->numberOfStates){
                STORM_LOG_WARN("    No best action found, as the successors could not be ordered.");
                return false;
            }
            auto nrOfSuccs = orderedSuccs.size();
            if (prMax) {
                STORM_LOG_INFO("   Interested in PrMax." << std::endl);
                // Check for the simple case
                auto simpleCheckResult = simpleActionCheck(state, orderedSuccs);
                if(simpleCheckResult.first == true) {
                    bestAct = simpleCheckResult.second;
                    STORM_LOG_INFO("Best action found");
                } else {
                    // else use SMT solver
                    std::vector<uint64_t> candidates;
                    uint_fast64_t index = 0;
                    auto numberOfOptionsForState = this->matrix.getRowGroupSize(state);
                    while (index < numberOfOptionsForState) {
                        auto rowA = this->matrix.getRow(this->matrix.getRowGroupIndices()[state] + index);
                        bool in = true;
                        for (uint_fast64_t i = 0; i < candidates.size(); i++){
                            auto rowB = this->matrix.getRow(this->matrix.getRowGroupIndices()[state] + candidates[i]);
                            auto compRes = actionSMTCompare(order, orderedSuccs, region, &rowA, &rowB);
                            if(compRes == GEQ){
                                candidates.erase(candidates.begin()+i);
                            } else if (compRes == LEQ) {
                                in = false;
                                // TODO put break; here?
                            }
                        }
                        if (in) {
                            candidates.push_back(index);
                        }
                        index++;
                    }
                    if (candidates.size() == 1) {
                        bestAct = candidates[0];
                        STORM_LOG_INFO("   More than 2 potential succs from 2 or more actions. Best action: " << bestAct << std::endl);
                    } else {
                        assert (false);
                        STORM_LOG_WARN("No best action found. Take action 0 as default.");
                    }
                }
//                }
            } else {
                // We are interested in PrMin
                STORM_LOG_INFO("   Interested in PrMin." << std::endl);
                if (nrOfSuccs == 2) {
                    uint64_t bestSucc = orderedSuccs[1];
                    boost::optional<storm::RationalFunction> bestFunc;
                    auto index = 0;
                    auto numberOfOptionsForState = this->matrix.getRowGroupSize(state);
                    while (index < numberOfOptionsForState) {
                        auto row = this->matrix.getRow(this->matrix.getRowGroupIndices()[state]);
                        auto itr = row.begin();
                        while (itr != row.end() && itr->getColumn() != bestSucc) {
                            itr++;
                        }
                        if (!bestFunc || (itr != row.end() && isFunctionGreaterEqual(bestFunc.get(), itr->getValue(), region))) {
                            bestFunc = itr->getValue();
                            bestAct = index;
                        }
                        index++;
                    }
                    STORM_LOG_INFO("   Two potential succs from 2 or more actions. Best action: " << bestAct << std::endl);

                } else {
                    // more than 2 succs
                    // Check for the simple case
                    // TODO do we need an extra vector for the reversed succs?
                    std::vector<uint64_t> revOrderedSuccs = std::vector<uint64_t>(orderedSuccs);
                    std::reverse(orderedSuccs.begin(), orderedSuccs.end());
                    auto simpleCheckResult = simpleActionCheck(state, revOrderedSuccs);
                    if(simpleCheckResult.first == true) {
                        bestAct = simpleCheckResult.second;
                    } else {
                        // else use SMT solver
                        std::vector<uint64_t> candidates;
                        auto index = 0;
                        auto numberOfOptionsForState = this->matrix.getRowGroupSize(state);
                        while (index < numberOfOptionsForState) {
                            auto rowA = this->matrix.getRow(this->matrix.getRowGroupIndices()[state] + index);
                            bool in = true;
                            for (uint_fast64_t i = 0; i < candidates.size(); i++){
                                auto rowB = this->matrix.getRow(this->matrix.getRowGroupIndices()[state] + candidates[i]);
                                auto compRes = actionSMTCompare(order, orderedSuccs, region, &rowA, &rowB);
                                if(compRes == LEQ){
                                    candidates.erase(candidates.begin()+i);
                                } else if (compRes == GEQ) {
                                    in = false;
                                    // TODO put break; here?
                                }
                            }
                            if (in) {
                                candidates.push_back(index);
                            }
                            index++;
                        }
                        if (candidates.size() == 1) {
                            bestAct = candidates [0];
                            STORM_LOG_INFO("   More than 2 potential succs from 2 or more actions. Best action: " << bestAct << std::endl);
                        } else {
                            STORM_LOG_WARN("No best action found. Take action 0 as default.");
                        }
                    }

                }
            }
            if (bestAct != this->matrix.getRowCount()) {
                order->addToMdpScheduler(state, bestAct);
                STORM_LOG_INFO("Best action for state " << state << ": " << bestAct);
                return true;
            }
            return false;
        }

        template<typename ValueType, typename ConstantType>
        bool ReachabilityOrderExtenderMdp<ValueType, ConstantType>::isFunctionGreaterEqual(storm::RationalFunction f1, storm::RationalFunction f2, storage::ParameterRegion<ValueType> region) {
            // We want to prove f1 >= f2, so we need UNSAT for f1 < f2
            std::shared_ptr<expressions::ExpressionManager> manager(new expressions::ExpressionManager());

            // Transform functions into expressions
            auto valueTypeToExpression = expressions::RationalFunctionToExpression<ValueType>(manager);
            auto exprF1 = valueTypeToExpression.toExpression(f1);
            auto exprF2 = valueTypeToExpression.toExpression(f2);

            // Add bounds for parameters from region
            expressions::Expression exprBounds = manager->boolean(true);
            auto variables = manager->getVariables();
            for (auto var : variables) {
                auto lb = utility::convertNumber<RationalNumber>(region.getLowerBoundary(var.getName()));
                auto ub = utility::convertNumber<RationalNumber>(region.getUpperBoundary(var.getName()));
                exprBounds = exprBounds && manager->rational(lb) < var && var < manager->rational(ub);
            }

            // Use SMTSolver
            auto exprToCheck = exprF1 < exprF2;
            solver::Z3SmtSolver s(*manager);
            s.add(exprToCheck);
            s.add(exprBounds);
            auto smtRes = s.check();

            // Evaluate Result
            if (smtRes == solver::SmtSolver::CheckResult::Unsat) {
                return true;
            } else {
                return false;
            }
        }

        template<typename ValueType, typename ConstantType>
        std::pair<uint64_t, uint64_t> ReachabilityOrderExtenderMdp<ValueType, ConstantType>::rangeOfSuccsForAction(typename storage::SparseMatrix<ValueType>::rows* action, std::vector<uint64_t> orderedSuccs){
            uint64_t start = orderedSuccs.size();
            uint64_t end = 0;
            for (auto entry : *action) {
                auto succ = entry.getColumn();
                for (uint64_t i = 0; i < orderedSuccs.size(); i++) {
                    if (succ == orderedSuccs[i] && i < start) {
                        start = i;
                    }
                    if (succ == orderedSuccs[i] && i > end) {
                        end = i;
                    }
                }
            }

            return std::make_pair(start,end);
        }

        template<typename ValueType, typename ConstantType>
        storage::BitVector ReachabilityOrderExtenderMdp<ValueType, ConstantType>::getHitSuccs(uint64_t state, uint64_t action, std::vector<uint64_t> orderedSuccs){
            storage::BitVector res = storage::BitVector(orderedSuccs.size(), false);
            for (auto succ : this->stateMap[state][action]) {
                for (uint64_t i = 0; i < orderedSuccs.size(); i++) {
                    if (succ == orderedSuccs[i]) {
                        res.set(i, true);
                    }
                }
            }

            return res;
        }

        template<typename ValueType, typename ConstantType>
        std::pair<bool, uint64_t> ReachabilityOrderExtenderMdp<ValueType, ConstantType>::simpleActionCheck(uint64_t state, std::vector<uint64_t> orderedSuccs){
            uint64_t noa = this->stateMap[state].size();
            uint64_t bestAct;
            std::vector<storage::BitVector> bitVecTable = std::vector<storage::BitVector>(noa);
            bool foundOne = false;
            for (uint_fast64_t i = 0; i < noa; i++) {
                storage::BitVector hitSuccs = getHitSuccs(state, i, orderedSuccs);
                if (hitSuccs[0]){
                    if (foundOne) {
                        return std::make_pair(false, 0);
                    } else {
                        bestAct = i;
                        foundOne = true;
                    }
                }
                bitVecTable[i] = hitSuccs;
            }
            storage::BitVector candidate = bitVecTable[bestAct];
            storage::BitVector others = storage::BitVector(orderedSuccs.size(), false);
            for (uint_fast64_t i = 0; i < noa; i++) {
                if(i != bestAct) others |= bitVecTable[i];
            }

            if ((candidate & others).empty()) return std::make_pair(true, bestAct);
            else return std::make_pair(false, 0);

        }

        template<typename ValueType, typename ConstantType>
        typename ReachabilityOrderExtenderMdp<ValueType, ConstantType>::ActionComparison ReachabilityOrderExtenderMdp<ValueType, ConstantType>::actionSMTCompare(
            std::shared_ptr<Order> order, const std::vector<uint64_t>& orderedSuccs, storage::ParameterRegion<ValueType>& region,
            ReachabilityOrderExtenderMdp::Rows action1, ReachabilityOrderExtenderMdp::Rows action2) {
            std::shared_ptr<expressions::ExpressionManager> manager(new expressions::ExpressionManager());

            // Get ordered vector of the succs actually occurring in the two actions
            std::vector<uint64_t> occSuccs = std::vector<uint64_t>();
            std::set<uint64_t> occSuccSet = std::set<uint64_t>();
            for (auto entry : *action1){
                occSuccSet.insert(entry.getColumn());
            }
            for (auto entry : *action2){
                occSuccSet.insert(entry.getColumn());
            }
            for (auto a : orderedSuccs) {
                if (occSuccSet.find(a) != occSuccSet.end()) {
                    occSuccs.push_back(a);
                }
            }

            // Turn everything we know about our succs into expressions
            expressions::Expression exprStateVars = manager->boolean(true);
            std::set<std::string> stateVarNames;
            for (uint_fast64_t i = 0; i < occSuccs.size(); i++) {
                std::string varName = "s" + std::to_string(occSuccs[i]);
                stateVarNames.insert(varName);
                auto var = manager->declareRationalVariable(varName);
                exprStateVars = exprStateVars && manager->rational(0) < var && var < manager->rational(1);
                if(i > 0) {
                    if (order->compare(occSuccs[i], occSuccs[i - 1]) == Order::SAME){
                        auto sameVar = manager->getVariable("s" + std::to_string(occSuccs[i-1]));
                        expressions::Expression exprSame = sameVar.getExpression() = var.getExpression();
                        exprStateVars = exprStateVars && exprSame;
                    } else {
                        auto biggerVar = manager->getVariable("s" + std::to_string(occSuccs[i-1]));
                        expressions::Expression exprBigger = biggerVar.getExpression() > var.getExpression();
                        exprStateVars = exprStateVars && exprBigger;
                    }
                }

            }

            // Turn rational functions into expressions
            auto valueTypeToExpression = expressions::RationalFunctionToExpression<ValueType>(manager);
            auto exprF1 = manager->rational(0);
            for (auto entry : *action1) {
                uint64_t column = entry.getColumn();
                std::string name = "s" + std::to_string(column);
                exprF1 = exprF1 + valueTypeToExpression.toExpression(entry.getValue()) * manager->getVariable(name);
            }
            auto exprF2 = manager->rational(0);
            for (auto entry : *action2) {
                uint64_t column = entry.getColumn();
                std::string name = "s" + std::to_string(column);
                exprF2 = exprF2 + valueTypeToExpression.toExpression(entry.getValue()) * manager->getVariable(name);
            }

            // Turn parameter bounds into expressions
            expressions::Expression exprParamBounds = manager->boolean(true);
            auto variables = manager->getVariables();
            for (auto var : variables) {
                std::string name = var.getName();
                if (stateVarNames.find(name) == stateVarNames.end()) {
                    auto lb = utility::convertNumber<RationalNumber>(region.getLowerBoundary(name));
                    auto ub = utility::convertNumber<RationalNumber>(region.getUpperBoundary(name));
                    exprParamBounds = exprParamBounds && manager->rational(lb) < var && var < manager->rational(ub);
                }
            }

            // Check if (action1 >= action2) -> check if (action2 > action1) is UNSAT. If yes --> GEQ. If no --> continue
            auto exprToCheck = exprF1 < exprF2;
            solver::Z3SmtSolver s1(*manager);
            s1.add(exprToCheck);
            s1.add(exprStateVars);
            s1.add(exprParamBounds);
            auto smtRes = s1.check();
            if (smtRes == solver::SmtSolver::CheckResult::Unsat) {
                return GEQ;
            }

            // Check if (action2 >= action1) -> check if (action1 > action2) is UNSAT. If yes --> LEQ. If no --> UNKNOWN
            exprToCheck = exprF2 < exprF1;
            solver::Z3SmtSolver s2(*manager);
            s2.add(exprToCheck);
            s2.add(exprStateVars);
            s2.add(exprParamBounds);
            smtRes = s2.check();
            if (smtRes == solver::SmtSolver::CheckResult::Unsat) {
                return LEQ;
            } else {
                return UNKNOWN;
            }

        }

        template class ReachabilityOrderExtenderMdp<RationalFunction, double>;
        template class ReachabilityOrderExtenderMdp<RationalFunction, RationalNumber>;

    }
}