#include "storm-pars/analysis/ReachabilityOrderExtender.h"
#include <storage/StronglyConnectedComponentDecomposition.h>

namespace storm {
namespace analysis {

template<typename ValueType, typename ConstantType>
ReachabilityOrderExtender<ValueType, ConstantType>::ReachabilityOrderExtender(std::shared_ptr<models::sparse::Model<ValueType>> model,
                                                                              std::shared_ptr<logic::Formula const> formula)
    : OrderExtender<ValueType, ConstantType>(model, formula) {
    this->assumptionMaker = new analysis::AssumptionMaker<ValueType, ConstantType>(this->matrix);
}

template<typename ValueType, typename ConstantType>
ReachabilityOrderExtender<ValueType, ConstantType>::ReachabilityOrderExtender(storm::storage::BitVector& topStates, storm::storage::BitVector& bottomStates,
                                                                              storm::storage::SparseMatrix<ValueType> matrix, bool prMax)
    : OrderExtender<ValueType, ConstantType>(topStates, bottomStates, matrix, prMax) {
    this->assumptionMaker = new analysis::AssumptionMaker<ValueType, ConstantType>(this->matrix);
}

template<typename ValueType, typename ConstantType>
ReachabilityOrderExtender<ValueType, ConstantType>::ReachabilityOrderExtender(storm::storage::BitVector& topStates, storm::storage::BitVector& bottomStates,
                                                                              storm::storage::SparseMatrix<ValueType> matrix)
    : OrderExtender<ValueType, ConstantType>(topStates, bottomStates, matrix, false) {
    this->assumptionMaker = new analysis::AssumptionMaker<ValueType, ConstantType>(this->matrix);
    STORM_LOG_ASSERT(this->deterministic, "Expecting model to be deterministic if prMax is not set");
}

template<typename ValueType, typename ConstantType>
void ReachabilityOrderExtender<ValueType, ConstantType>::handleOneSuccessor(std::shared_ptr<Order> order, uint_fast64_t currentState, uint_fast64_t successor) {
    STORM_LOG_ASSERT(order->contains(successor), "Can't handle state with one successor if successor is not contained in order");
    if (currentState != successor) {
        if (order->contains(currentState)) {
            order->merge(currentState, successor);
        } else {
            order->addToNode(currentState, order->getNode(successor));
        }
    }
}

template<typename ValueType, typename ConstantType>
void ReachabilityOrderExtender<ValueType, ConstantType>::setBottomTopStates() {
    if (this->bottomStates == boost::none || this->topStates == boost::none) {
        STORM_LOG_ASSERT(this->model != nullptr, "Can't get initial order if model is not specified");
        modelchecker::SparsePropositionalModelChecker<models::sparse::Model<ValueType>> propositionalChecker(*(this->model));
        storage::BitVector phiStates;
        storage::BitVector psiStates;
        STORM_LOG_ASSERT(this->formula->isProbabilityOperatorFormula(), "Can't get initial order if formula is not a probability operator formula");
        if (this->formula->asProbabilityOperatorFormula().getSubformula().isUntilFormula()) {
            phiStates = propositionalChecker.check(this->formula->asProbabilityOperatorFormula().getSubformula().asUntilFormula().getLeftSubformula())
                            ->asExplicitQualitativeCheckResult()
                            .getTruthValuesVector();
            psiStates = propositionalChecker.check(this->formula->asProbabilityOperatorFormula().getSubformula().asUntilFormula().getRightSubformula())
                            ->asExplicitQualitativeCheckResult()
                            .getTruthValuesVector();
        } else {
            STORM_LOG_ASSERT(this->formula->asProbabilityOperatorFormula().getSubformula().isEventuallyFormula(),
                             "Expecting formula to be until or eventually formula");
            phiStates = storage::BitVector(this->numberOfStates, true);
            psiStates = propositionalChecker.check(this->formula->asProbabilityOperatorFormula().getSubformula().asEventuallyFormula().getSubformula())
                            ->asExplicitQualitativeCheckResult()
                            .getTruthValuesVector();
        }
        // Get the maybeStates
        std::pair<storage::BitVector, storage::BitVector> statesWithProbability01 =
            utility::graph::performProb01(this->model->getBackwardTransitions(), phiStates, psiStates);
        this->topStates = std::move(statesWithProbability01.second);
        this->bottomStates = std::move(statesWithProbability01.first);

        STORM_LOG_THROW(this->topStates->begin() != this->topStates->end(), exceptions::NotSupportedException, "Formula yields to no 1 states");
        STORM_LOG_THROW(this->bottomStates->begin() != this->bottomStates->end(), exceptions::NotSupportedException, "Formula yields to no zero states");
    }
}

template<typename ValueType, typename ConstantType>
void ReachabilityOrderExtender<ValueType, ConstantType>::checkRewardsForOrder(std::shared_ptr<Order> order) {
    // Intentionally left empty
}

template<typename ValueType, typename ConstantType>
std::pair<uint_fast64_t, uint_fast64_t> ReachabilityOrderExtender<ValueType, ConstantType>::extendByBackwardReasoning(
    std::shared_ptr<Order> order, storm::storage::ParameterRegion<ValueType> region, uint_fast64_t currentState) {
    std::vector<uint_fast64_t> sortedSuccs;
    auto const& successors = this->getSuccessors(currentState, order);

    auto temp = order->sortStatesUnorderedPair(successors);
    if (temp.first.first != this->numberOfStates) {
        return temp.first;
    }
    sortedSuccs = std::move(temp.second);

    if (order->compare(sortedSuccs[0], sortedSuccs[sortedSuccs.size() - 1]) == Order::SAME) {
        if (!order->contains(currentState)) {
            order->addToNode(currentState, order->getNode(sortedSuccs[0]));
        } else {
            order->merge(currentState, sortedSuccs[0]);
        }
    } else {
        if (!order->contains(sortedSuccs[0])) {
            assert(order->isBottomState(sortedSuccs[sortedSuccs.size() - 1]));
            assert(sortedSuccs.size() == 2);
            order->addAbove(sortedSuccs[0], order->getBottom());
        }
        if (!order->contains(sortedSuccs[sortedSuccs.size() - 1])) {
            assert(order->isTopState(sortedSuccs[0]));
            assert(sortedSuccs.size() == 2);
            order->addBelow(sortedSuccs[sortedSuccs.size() - 1], order->getTop());
        }
        // sortedSuccs[0] is highest
        if (!order->contains(currentState)) {
            order->addBetween(currentState, sortedSuccs[0], sortedSuccs[sortedSuccs.size() - 1]);
        } else {
            order->addRelation(sortedSuccs[0], currentState);
            order->addRelation(currentState, sortedSuccs[sortedSuccs.size() - 1]);
        }
    }
    assert(order->contains(currentState) && order->compare(order->getNode(currentState), order->getBottom()) == Order::ABOVE &&
           order->compare(order->getNode(currentState), order->getTop()) == Order::BELOW);
    return {this->numberOfStates, this->numberOfStates};
}

template<typename ValueType, typename ConstantType>
std::pair<uint_fast64_t, uint_fast64_t> ReachabilityOrderExtender<ValueType, ConstantType>::extendByForwardReasoning(
    std::shared_ptr<Order> order, storm::storage::ParameterRegion<ValueType> region, uint_fast64_t currentState) {
    STORM_LOG_ASSERT(order->contains(currentState), "Can't apply forward reasoning if order doesn't contain current state");
    STORM_LOG_ASSERT(this->cyclic, "Needs cyclic model for forward reasoning");

    std::pair<std::pair<uint_fast64_t, uint_fast64_t>, std::vector<uint_fast64_t>> sorted = this->sortForFowardReasoning(currentState, order);
    uint_fast64_t s1 = sorted.first.first;
    uint_fast64_t s2 = sorted.first.second;
    std::vector<uint_fast64_t>& statesSorted = sorted.second;
    if (s1 == this->numberOfStates) {
        STORM_LOG_ASSERT(statesSorted.size() == this->getSuccessors(currentState, order).size() + 1, "Expecting all states to be sorted, done for now");
        // all could be sorted, no need to do anything
    } else if (s2 == this->numberOfStates) {
        if (!order->contains(s1)) {
            order->add(s1);
        }
        if (statesSorted[0] == currentState) {
            order->addRelation(s1, statesSorted[0]);
            order->addRelation(s1, statesSorted[statesSorted.size() - 1]);  //, allowMerge);
            order->addStateToHandle(s1);
        } else if (statesSorted[statesSorted.size() - 1] == currentState) {
            order->addRelation(statesSorted[0], s1);                        //, allowMerge);
            order->addRelation(statesSorted[statesSorted.size() - 1], s1);  //, allowMerge);
            order->addStateToHandle(s1);
        } else {
            bool continueSearch = true;
            for (auto& entry : this->matrix.getRow(currentState)) {
                if (entry.getColumn() == s1) {
                    if (entry.getValue().isConstant()) {
                        continueSearch = false;
                    }
                }
            }
            if (continueSearch) {
                for (auto& i : statesSorted) {
                    if (order->compare(i, s1) == Order::UNKNOWN) {
                        return {i, s1};
                    }
                }
            }
        }
    } else {
        return {s1, s2};
    }
    assert(order->contains(currentState) && order->compare(order->getNode(currentState), order->getBottom()) == Order::ABOVE &&
           order->compare(order->getNode(currentState), order->getTop()) == Order::BELOW);
    return {this->numberOfStates, this->numberOfStates};
}

template class ReachabilityOrderExtender<RationalFunction, double>;
template class ReachabilityOrderExtender<RationalFunction, RationalNumber>;
}  // namespace analysis
}  // namespace storm