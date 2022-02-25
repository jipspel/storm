#include "storm-pars/analysis/ReachabilityOrderExtenderDtmc.h"

namespace storm {
    namespace analysis {

        template<typename ValueType, typename ConstantType>
    ReachabilityOrderExtenderDtmc<ValueType, ConstantType>::ReachabilityOrderExtenderDtmc(std::shared_ptr<models::sparse::Model<ValueType>> model, std::shared_ptr<logic::Formula const> formula) : ReachabilityOrderExtender<ValueType, ConstantType>(model, formula) {
            // intentionally left empty
        }

        template<typename ValueType, typename ConstantType>
        ReachabilityOrderExtenderDtmc<ValueType, ConstantType>::ReachabilityOrderExtenderDtmc(storm::storage::BitVector& topStates,  storm::storage::BitVector& bottomStates, storm::storage::SparseMatrix<ValueType> matrix) : ReachabilityOrderExtender<ValueType, ConstantType>(topStates, bottomStates, matrix) {
            // intentionally left empty
        }

        template <typename ValueType, typename ConstantType>
        std::tuple<std::shared_ptr<Order>, uint_fast64_t, uint_fast64_t> ReachabilityOrderExtenderDtmc<ValueType, ConstantType>::extendOrder(std::shared_ptr<Order> order, storm::storage::ParameterRegion<ValueType> region, std::shared_ptr<MonotonicityResult<VariableType>> monRes, std::shared_ptr<expressions::BinaryRelationExpression> assumption) {
            STORM_LOG_ASSERT (order != nullptr, "Order should be provided");
            if (assumption != nullptr) {
                this->handleAssumption(order, assumption);
            }

            auto currentStateMode = this->getNextState(order, this->numberOfStates, false);
            while (currentStateMode.first != this->numberOfStates) {
                STORM_LOG_ASSERT (currentStateMode.first < this->numberOfStates, "Unexpected state number");
                auto& currentState = currentStateMode.first;
                auto const & successors = this->getSuccessors(currentState);
                std::pair<uint_fast64_t, uint_fast64_t> result =  {this->numberOfStates, this->numberOfStates};

                if (successors.size() == 1) {
                    STORM_LOG_ASSERT (order->contains(successors[0]), "Expecting order to contain successor of state " << currentState);
                    this->handleOneSuccessor(order, currentState, successors[0]);
                } else if (!successors.empty()) {
                    if (order->isOnlyInitialOrder()) {
                        order->add(currentState);
                        if (!order->isTrivial(currentState)) {
                            // This state is part of an scc, therefore, we could do forward reasoning here
                            result = this->extendByForwardReasoning(order, region, currentState);
                        } else {
                            result = {this->numberOfStates, this->numberOfStates};
                        }
                    } else {
                        result = this->extendNormal(order, region, currentState);
                    }
                }

                if (result.first == this->numberOfStates) {
                    // We did extend the order
                    STORM_LOG_ASSERT (result.second == this->numberOfStates, "Expecting both parts of result to contain the number of states");
                    STORM_LOG_ASSERT (order->sortStates(successors).size() == successors.size(), "Something went wrong while sorting states, number of states differs");
                    STORM_LOG_ASSERT (order->contains(currentState) && order->getNode(currentState) != nullptr, "Expecting order to contain the current State");

                    if (monRes != nullptr) {
                        for (auto& param : this->occuringVariablesAtState[currentState]) {
                            this->checkParOnStateMonRes(currentState, order, param, region, monRes);
                        }
                    }
                    // Get the next state
                    currentStateMode = this->getNextState(order, currentState, true);
                } else {
                    STORM_LOG_ASSERT (result.first < this->numberOfStates && result.second < this->numberOfStates, "Expecting both result numbers to correspond to states");
                    STORM_LOG_ASSERT (order->compare(result.first, result.second) == Order::UNKNOWN && order->compare(result.second, result.first) == Order::UNKNOWN, "Expecting relation between the two states to be unknown");
                    // Try to add states based on min/max and assumptions, only if we are not in statesToHandle mode
                    if (currentStateMode.second && this->extendWithAssumption(order, region, result.first, result.second)) {
                        continue;
                    }
                    // We couldn't extend the order
                    if (this->nonParametricStates.find(currentState) != this->nonParametricStates.end()) {
                        if (!order->contains(currentState)) {
                            // State is not parametric, so we hope that just adding it between =) and =( will help us
                            order->add(currentState);
                        }
                        currentStateMode = this->getNextState(order, currentState, true);
                        continue;
                    } else {
                        if (!currentStateMode.second) {
                            // The state was based on statesToHandle, so it is not bad if we cannot continue with this.
                            currentStateMode = this->getNextState(order, currentState, false);
                            continue;
                        } else {
                            // The state was based on the topological sorting, so we need to return, but first add this state to the states Sorted as we are not done with it
                            order->addStateSorted(currentState);
                            this->continueExtending[order] = false;
                            return {order, result.first, result.second};
                        }
                    }
                }
                STORM_LOG_ASSERT (order->sortStates(successors).size() == successors.size(), "Expecting all successor states to be sorted");
            }

            STORM_LOG_ASSERT (order->getDoneBuilding(), "Expecting to have a final order");
            if (monRes != nullptr) {
                // monotonicity result for the in-build checking of monotonicity
                monRes->setDone();
            }
            return std::make_tuple(order, this->numberOfStates, this->numberOfStates);
        }

        template class ReachabilityOrderExtenderDtmc<RationalFunction, double>;
        template class ReachabilityOrderExtenderDtmc<RationalFunction, RationalNumber>;

    }
}