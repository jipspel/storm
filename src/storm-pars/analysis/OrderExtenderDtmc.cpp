#include "storm-pars/analysis/OrderExtenderDtmc.h"


namespace storm {
    namespace analysis {

        template<typename ValueType, typename ConstantType>
        OrderExtenderDtmc<ValueType, ConstantType>::OrderExtenderDtmc(std::shared_ptr<models::sparse::Model<ValueType>> model, std::shared_ptr<logic::Formula const> formula) : ReachabilityOrderExtender<ValueType, ConstantType>(model, formula) {
            // intentionally left empty
        }

        template<typename ValueType, typename ConstantType>
        OrderExtenderDtmc<ValueType, ConstantType>::OrderExtenderDtmc(storm::storage::BitVector* topStates,  storm::storage::BitVector* bottomStates, storm::storage::SparseMatrix<ValueType> matrix) : ReachabilityOrderExtender<ValueType, ConstantType>(topStates, bottomStates, matrix) {
            // intentionally left empty
        }

        template<typename ValueType, typename ConstantType>
        std::pair<uint_fast64_t, uint_fast64_t> OrderExtenderDtmc<ValueType, ConstantType>::extendByBackwardReasoning(std::shared_ptr<Order> order, uint_fast64_t currentState) {
            std::vector<uint64_t> successors = this->stateMap[currentState][0]; // Get succs
            return ReachabilityOrderExtender<ValueType, ConstantType>::extendByBackwardReasoning(order, currentState, successors, false); // Call Base Class function.

        }

        template class OrderExtenderDtmc<RationalFunction, double>;
        template class OrderExtenderDtmc<RationalFunction, RationalNumber>;

    }
}