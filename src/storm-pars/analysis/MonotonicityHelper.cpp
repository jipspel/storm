#include "MonotonicityHelper.h"

#include "storm/exceptions/NotSupportedException.h"
#include "storm/exceptions/InvalidOperationException.h"

#include "storm/models/ModelType.h"

#include "storm/modelchecker/results/CheckResult.h"

#include "storm/modelchecker/results/ExplicitQuantitativeCheckResult.h"

#include "storm-pars/analysis/AssumptionChecker.h"


namespace storm {
    namespace analysis {
        /*** Constructor ***/
        template <typename ValueType, typename ConstantType>
        MonotonicityHelper<ValueType, ConstantType>::MonotonicityHelper(std::shared_ptr<models::sparse::Model<ValueType>> model, std::vector<std::shared_ptr<logic::Formula const>> formulas, std::vector<storage::ParameterRegion<ValueType>> regions, uint_fast64_t numberOfSamples, double const& precision, bool dotOutput) : assumptionMaker(model->getTransitionMatrix()){
            assert (model != nullptr);
            STORM_LOG_THROW(regions.size() <= 1, exceptions::NotSupportedException, "Monotonicity checking is not (yet) supported for multiple regions");
            STORM_LOG_THROW(formulas.size() <= 1, exceptions::NotSupportedException, "Monotonicity checking is not (yet) supported for multiple formulas");

            this->model = model;
            this->formulas = formulas;
            this->precision = utility::convertNumber<ConstantType>(precision);
            this->matrix = model->getTransitionMatrix();
            this->dotOutput = dotOutput;

            if (regions.size() == 1) {
                this->region = *(regions.begin());
            } else {
                typename storage::ParameterRegion<ValueType>::Valuation lowerBoundaries;
                typename storage::ParameterRegion<ValueType>::Valuation upperBoundaries;
                std::set<VariableType> vars;
                vars = models::sparse::getProbabilityParameters(*model);
                for (auto var : vars) {
                    typename storage::ParameterRegion<ValueType>::CoefficientType lb = utility::convertNumber<CoefficientType>(0 + precision) ;
                    typename storage::ParameterRegion<ValueType>::CoefficientType ub = utility::convertNumber<CoefficientType>(1 - precision) ;
                    lowerBoundaries.insert(std::make_pair(var, lb));
                    upperBoundaries.insert(std::make_pair(var, ub));
                }
                this->region = storage::ParameterRegion<ValueType>(std::move(lowerBoundaries), std::move(upperBoundaries));
            }

            if (numberOfSamples > 2) {
                // sampling
                if (model->isOfType(models::ModelType::Dtmc)) {
                    this->resultCheckOnSamples = std::map<VariableType, std::pair<bool, bool>>(
                            checkMonotonicityOnSamples(model->template as<models::sparse::Dtmc<ValueType>>(), numberOfSamples));
                } else if (model->isOfType(models::ModelType::Mdp)) {
                    this->resultCheckOnSamples = std::map<VariableType, std::pair<bool, bool>>(
                            checkMonotonicityOnSamples(model->template as<models::sparse::Mdp<ValueType>>(), numberOfSamples));
                }
               // TODO use samples also for assumptinomaker

                checkSamples = true;
            } else {
                if (numberOfSamples > 0) {
                    STORM_LOG_WARN("At least 3 sample points are needed to check for monotonicity on samples, not using samples for now");
                }
                checkSamples = false;
            }

            this->extender = new analysis::OrderExtender<ValueType, ConstantType>(model, formulas[0], region);
        }


        /*** Public methods ***/
        template <typename ValueType, typename ConstantType>
        std::map<std::shared_ptr<Order>, std::pair<std::shared_ptr<MonotonicityResult<typename MonotonicityHelper<ValueType, ConstantType>::VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>> MonotonicityHelper<ValueType, ConstantType>::checkMonotonicityInBuild(std::ostream& outfile, std::string dotOutfileName) {
            createOrder();

            //output of results
            for (auto itr : monResults) {
                std::string temp = itr.second.first->toString();
                outfile << temp << std::endl;
            }

            //dotoutput
            if (dotOutput) {
                STORM_LOG_WARN_COND(monResults.size() <= 10, "Too many Reachability Orders. Dot Output will only be created for 10.");
                int i = 0;
                auto orderItr = monResults.begin();
                while (i < 10 && orderItr != monResults.end()) {
                    std::ofstream dotOutfile;
                    std::string name = dotOutfileName + std::to_string(i);
                    utility::openFile(name, dotOutfile);
                    dotOutfile << "Assumptions:" << std::endl;
                    auto assumptionItr = orderItr->second.second.begin();
                    while (assumptionItr != orderItr->second.second.end()) {
                        dotOutfile << *assumptionItr << std::endl;
                        dotOutfile << std::endl;
                        assumptionItr++;
                    }
                    dotOutfile << std::endl;
                    orderItr->first->dotOutputToFile(dotOutfile);
                    utility::closeFile(dotOutfile);
                    i++;
                    orderItr++;
                }
            }
            return monResults;
        }

        /*** Private methods ***/
        template <typename ValueType, typename ConstantType>
        void MonotonicityHelper<ValueType, ConstantType>::createOrder() {
            // Transform to Orders
            std::tuple<std::shared_ptr<Order>, uint_fast64_t, uint_fast64_t> criticalTuple;

            // Create initial order
            auto monRes = std::make_shared<MonotonicityResult<VariableType>>(MonotonicityResult<VariableType>());
            criticalTuple = extender->toOrder(monRes);
            // Continue based on not (yet) sorted states
            std::map<std::shared_ptr<Order>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>> result;

            auto val1 = std::get<1>(criticalTuple);
            auto val2 = std::get<2>(criticalTuple);
            auto numberOfStates = model->getNumberOfStates();
            std::vector<std::shared_ptr<expressions::BinaryRelationExpression>> assumptions;

            if (val1 == numberOfStates && val2 == numberOfStates) {
                auto resAssumptionPair = std::pair<std::shared_ptr<MonotonicityResult<VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>(monRes, assumptions);
                monResults.insert(std::pair<std::shared_ptr<Order>, std::pair<std::shared_ptr<MonotonicityResult<VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>>(std::get<0>(criticalTuple), resAssumptionPair));
            } else if (val1 != numberOfStates && val2 != numberOfStates) {
                extendOrderWithAssumptions(std::get<0>(criticalTuple), val1, val2, assumptions, monRes);
            } else {
                assert (false);
            }
        }

        template <typename ValueType, typename ConstantType>
        void MonotonicityHelper<ValueType, ConstantType>::extendOrderWithAssumptions(std::shared_ptr<Order> order, uint_fast64_t val1, uint_fast64_t val2, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>> assumptions, std::shared_ptr<MonotonicityResult<VariableType>> monRes) {
            std::map<std::shared_ptr<Order>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>> result;

            auto numberOfStates = model->getNumberOfStates();
            if (val1 == numberOfStates || val2 == numberOfStates) {
                assert (val1 == val2);
                assert (order->getAddedStates()->size() == order->getAddedStates()->getNumberOfSetBits());
                auto resAssumptionPair = std::pair<std::shared_ptr<MonotonicityResult<VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>(monRes, assumptions);
                monResults.insert(std::pair<std::shared_ptr<Order>, std::pair<std::shared_ptr<MonotonicityResult<VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>>(std::move(order), std::move(resAssumptionPair)));
            } else {
                // Make the three assumptions
                auto newAssumptions = assumptionMaker.createAndCheckAssumptions(val1, val2, order, region);
                assert (newAssumptions.size() <= 3);
                auto itr = newAssumptions.begin();

                while (itr != newAssumptions.end()) {
                    auto assumption = *itr;
                    ++itr;
                    if (assumption.second != AssumptionStatus::INVALID) {
                        if (itr != newAssumptions.end()) {
                            // We make a copy of the order and the assumptions
                            // TODO: @Svenja create a copy method for order in the same way as for monResCopy, with the content of the Order Constructor.
                            auto orderCopy = std::shared_ptr<Order>(order);
                            auto assumptionsCopy = std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>(assumptions);
                            auto monResCopy = monRes->copy();

                            if (assumption.second == AssumptionStatus::UNKNOWN) {
                                // only add assumption to the set of assumptions if it is unknown whether it holds or not
                                assumptionsCopy.push_back(std::move(assumption.first));
                            }

                            auto criticalTuple = extender->extendOrder(orderCopy, monResCopy, assumption.first);
                            if (monResCopy->isSomewhereMonotonicity()) {
                                extendOrderWithAssumptions(std::get<0>(criticalTuple), std::get<1>(criticalTuple), std::get<2>(criticalTuple), assumptionsCopy, monResCopy);
                            }
                        } else {
                            // It is the last one, so we don't need to create a copy.
                            if (assumption.second == AssumptionStatus::UNKNOWN) {
                                // only add assumption to the set of assumptions if it is unknown whether it holds or not
                                assumptions.push_back(std::move(assumption.first));
                            }

                            auto criticalTuple = extender->extendOrder(order, monRes, assumption.first);
                            if (monRes->isSomewhereMonotonicity()) {
                                extendOrderWithAssumptions(std::get<0>(criticalTuple), std::get<1>(criticalTuple), std::get<2>(criticalTuple), assumptions, monRes);
                            }
                        }
                    }
                }
                if (this->monResults.size() == 0) {
                    auto resAssumptionPair = std::pair<std::shared_ptr<MonotonicityResult<VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>(monRes, assumptions);
                    monResults.insert(std::pair<std::shared_ptr<Order>, std::pair<std::shared_ptr<MonotonicityResult<VariableType>>, std::vector<std::shared_ptr<expressions::BinaryRelationExpression>>>>(std::move(order), std::move(resAssumptionPair)));
                }
            }
        }

        template <typename ValueType, typename ConstantType>
        std::map<typename MonotonicityHelper<ValueType, ConstantType>::VariableType, std::pair<bool, bool>> MonotonicityHelper<ValueType, ConstantType>::checkMonotonicityOnSamples(std::shared_ptr<models::sparse::Dtmc<ValueType>> model, uint_fast64_t numberOfSamples) {
            assert (numberOfSamples > 2);
            std::map<VariableType, std::pair<bool, bool>> result;

            auto instantiator = utility::ModelInstantiator<models::sparse::Dtmc<ValueType>, models::sparse::Dtmc<ConstantType>>(*model);
            std::set<VariableType> variables = models::sparse::getProbabilityParameters(*model);

            // For each of the variables create a model in which we only change the value for this specific variable
            for (auto itr = variables.begin(); itr != variables.end(); ++itr) {
                ConstantType previous = -1;
                bool monDecr = true;
                bool monIncr = true;

                // Check monotonicity in variable (*itr) by instantiating the model
                // all other variables fixed on lb, only increasing (*itr)
                for (uint_fast64_t i = 0; (monDecr || monIncr) && i < numberOfSamples; ++i) {
                    // Create valuation
                    auto valuation = utility::parametric::Valuation<ValueType>();
                    for (auto itr2 = variables.begin(); itr2 != variables.end(); ++itr2) {
                        // Only change value for current variable
                        if ((*itr) == (*itr2)) {
                            auto lb = region.getLowerBoundary(itr->name());
                            auto ub = region.getUpperBoundary(itr->name());
                            // Creates samples between lb and ub, that is: lb, lb + (ub-lb)/(#samples -1), lb + 2* (ub-lb)/(#samples -1), ..., ub
                            valuation[*itr2] = utility::convertNumber<typename utility::parametric::CoefficientType<ValueType>::type>(lb + i*(ub-lb)/(numberOfSamples-1));
                        } else {
                            auto lb = region.getLowerBoundary(itr->name());
                            valuation[*itr2] = utility::convertNumber<typename utility::parametric::CoefficientType<ValueType>::type>(lb);
                        }
                    }

                    // Instantiate model and get result
                    models::sparse::Dtmc<ConstantType> sampleModel = instantiator.instantiate(valuation);
                    auto checker = modelchecker::SparseDtmcPrctlModelChecker<models::sparse::Dtmc<ConstantType>>(sampleModel);
                    std::unique_ptr<modelchecker::CheckResult> checkResult;
                    auto formula = formulas[0];
                    if (formula->isProbabilityOperatorFormula() && formula->asProbabilityOperatorFormula().getSubformula().isUntilFormula()) {
                        const modelchecker::CheckTask<logic::UntilFormula, ConstantType> checkTask = modelchecker::CheckTask<logic::UntilFormula, ConstantType>((*formula).asProbabilityOperatorFormula().getSubformula().asUntilFormula());
                        checkResult = checker.computeUntilProbabilities(Environment(), checkTask);
                    } else if (formula->isProbabilityOperatorFormula() && formula->asProbabilityOperatorFormula().getSubformula().isEventuallyFormula()) {
                        const modelchecker::CheckTask<logic::EventuallyFormula, ConstantType> checkTask = modelchecker::CheckTask<logic::EventuallyFormula, ConstantType>((*formula).asProbabilityOperatorFormula().getSubformula().asEventuallyFormula());
                        checkResult = checker.computeReachabilityProbabilities(Environment(), checkTask);
                    } else {
                        STORM_LOG_THROW(false, exceptions::NotSupportedException, "Expecting until or eventually formula");
                    }

                    auto quantitativeResult = checkResult->asExplicitQuantitativeCheckResult<ConstantType>();
                    std::vector<ConstantType> values = quantitativeResult.getValueVector();
                    auto initialStates = model->getInitialStates();
                    ConstantType initial = 0;
                    // Get total probability from initial states
                    for (auto j = initialStates.getNextSetIndex(0); j < model->getNumberOfStates(); j = initialStates.getNextSetIndex(j + 1)) {
                        initial += values[j];
                    }
                    // Calculate difference with result for previous valuation
                    assert (initial >= 0 - precision && initial <= 1 + precision);
                    ConstantType diff = previous - initial;
                    assert (previous == -1 || diff >= -1 - precision && diff <= 1 + precision);

                    if (previous != -1 && (diff > precision || diff < -precision)) {
                        monDecr &= diff > precision; // then previous value is larger than the current value from the initial states
                        monIncr &= diff < -precision;
                    }
                    previous = initial;
                }
                result.insert(std::pair<VariableType,  std::pair<bool, bool>>(*itr, std::pair<bool,bool>(monIncr, monDecr)));
            }
            resultCheckOnSamples = result;
            return result;
        }

        template <typename ValueType, typename ConstantType>
        std::map<typename MonotonicityHelper<ValueType, ConstantType>::VariableType, std::pair<bool, bool>> MonotonicityHelper<ValueType, ConstantType>::checkMonotonicityOnSamples(std::shared_ptr<models::sparse::Mdp<ValueType>> model, uint_fast64_t numberOfSamples) {
            assert(numberOfSamples > 2);
            std::map<VariableType, std::pair<bool, bool>> result;

            auto instantiator = utility::ModelInstantiator<models::sparse::Mdp<ValueType>, models::sparse::Mdp<ConstantType>>(*model);
            std::set<VariableType> variables =  models::sparse::getProbabilityParameters(*model);

            // For each of the variables create a model in which we only change the value for this specific variable
            for (auto itr = variables.begin(); itr != variables.end(); ++itr) {
                ConstantType previous = -1;
                bool monDecr = true;
                bool monIncr = true;

                // Check monotonicity in variable (*itr) by instantiating the model
                // all other variables fixed on lb, only increasing (*itr)
                for (uint_fast64_t i = 0; (monDecr || monIncr) && i < numberOfSamples; ++i) {
                    // Create valuation
                    auto valuation = utility::parametric::Valuation<ValueType>();
                    for (auto itr2 = variables.begin(); itr2 != variables.end(); ++itr2) {
                        // Only change value for current variable
                        if ((*itr) == (*itr2)) {
                            auto lb = region.getLowerBoundary(itr->name());
                            auto ub = region.getUpperBoundary(itr->name());
                            // Creates samples between lb and ub, that is: lb, lb + (ub-lb)/(#samples -1), lb + 2* (ub-lb)/(#samples -1), ..., ub
                            valuation[*itr2] = utility::convertNumber<typename utility::parametric::CoefficientType<ValueType>::type>(lb + i*(ub-lb)/(numberOfSamples-1));
                        } else {
                            auto lb = region.getLowerBoundary(itr->name());
                            valuation[*itr2] = utility::convertNumber<typename utility::parametric::CoefficientType<ValueType>::type>(lb);
                        }
                    }

                    // Instantiate model and get result
                    models::sparse::Mdp<ConstantType> sampleModel = instantiator.instantiate(valuation);
                    auto checker = modelchecker::SparseMdpPrctlModelChecker<models::sparse::Mdp<ConstantType>>(sampleModel);
                    std::unique_ptr<modelchecker::CheckResult> checkResult;
                    auto formula = formulas[0];
                    if (formula->isProbabilityOperatorFormula() && formula->asProbabilityOperatorFormula().getSubformula().isUntilFormula()) {
                        const modelchecker::CheckTask<logic::UntilFormula, ConstantType> checkTask = modelchecker::CheckTask<logic::UntilFormula, ConstantType>((*formula).asProbabilityOperatorFormula().getSubformula().asUntilFormula());
                        checkResult = checker.computeUntilProbabilities(Environment(), checkTask);
                    } else if (formula->isProbabilityOperatorFormula() && formula->asProbabilityOperatorFormula().getSubformula().isEventuallyFormula()) {
                        const modelchecker::CheckTask<logic::EventuallyFormula, ConstantType> checkTask = modelchecker::CheckTask<logic::EventuallyFormula, ConstantType>((*formula).asProbabilityOperatorFormula().getSubformula().asEventuallyFormula());
                        checkResult = checker.computeReachabilityProbabilities(Environment(), checkTask);
                    } else {
                        STORM_LOG_THROW(false, exceptions::NotSupportedException, "Expecting until or eventually formula");
                    }

                    auto quantitativeResult = checkResult->asExplicitQuantitativeCheckResult<ConstantType>();
                    std::vector<ConstantType> values = quantitativeResult.getValueVector();
                    auto initialStates = model->getInitialStates();
                    ConstantType initial = 0;
                    // Get total probability from initial states
                    for (auto j = initialStates.getNextSetIndex(0); j < model->getNumberOfStates(); j = initialStates.getNextSetIndex(j + 1)) {
                        initial += values[j];
                    }
                    // Calculate difference with result for previous valuation
                    assert (initial >= 0 - precision && initial <= 1 + precision);
                    ConstantType diff = previous - initial;
                    assert (previous == -1 || diff >= -1 - precision && diff <= 1 + precision);

                    if (previous != -1 && (diff > precision || diff < -precision)) {
                        monDecr &= diff > precision; // then previous value is larger than the current value from the initial states
                        monIncr &= diff < -precision;
                    }
                    previous = initial;
                }
                result.insert(std::pair<VariableType,  std::pair<bool, bool>>(*itr, std::pair<bool,bool>(monIncr, monDecr)));
            }
            resultCheckOnSamples = result;
            return result;
        }

        template class MonotonicityHelper<RationalFunction, double>;
        template class MonotonicityHelper<RationalFunction, RationalNumber>;
    }
}
