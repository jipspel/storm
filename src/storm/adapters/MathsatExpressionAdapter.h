#ifndef STORM_ADAPTERS_MATHSATEXPRESSIONADAPTER_H_
#define STORM_ADAPTERS_MATHSATEXPRESSIONADAPTER_H_

#include "storm-config.h"

#include <stack>

#ifdef STORM_HAVE_MSAT
#include "mathsat.h"
#endif

#include "storage/expressions/ExpressionManager.h"
#include "storage/expressions/Expressions.h"
#include "storage/expressions/ExpressionVisitor.h"
#include "storm/utility/macros.h"
#include "storm/exceptions/ExpressionEvaluationException.h"
#include "storm/exceptions/InvalidTypeException.h"
#include "storm/exceptions/InvalidArgumentException.h"
#include "storm/exceptions/NotImplementedException.h"

#ifdef STORM_HAVE_MSAT
namespace std {
    // Define hashing operator for MathSAT's declarations.
    template <>
    struct hash<msat_decl> {
        size_t operator()(msat_decl const& declaration) const {
            return hash<void*>()(declaration.repr);
        }
    };
}

// Define equality operator to make hashing work.
bool operator==(msat_decl decl1, msat_decl decl2);
#endif

namespace storm {
	namespace adapters {

#ifdef STORM_HAVE_MSAT
        
		class MathsatExpressionAdapter : public storm::expressions::ExpressionVisitor {
		public:
            /*!
             * Creates an expression adapter that can translate expressions to the format of MathSAT.
			 *
             * @param manager The manager that can be used to build expressions.
             * @param env The MathSAT environment in which to build the expressions.
             */
			MathsatExpressionAdapter(storm::expressions::ExpressionManager& manager, msat_env& env) : manager(manager), env(env), variableToDeclarationMapping() {
				// Intentionally left empty.
			}

			/*!
             * Translates the given expression to an equivalent term for MathSAT.
             *
             * @param expression The expression to be translated.
             * @return An equivalent term for MathSAT.
             */
			msat_term translateExpression(storm::expressions::Expression const& expression) {
                msat_term result = boost::any_cast<msat_term>(expression.getBaseExpression().accept(*this, boost::none));
                if (MSAT_ERROR_TERM(result)) {
                    std::string errorMessage(msat_last_error_message(env));
                    STORM_LOG_THROW(!MSAT_ERROR_TERM(result), storm::exceptions::ExpressionEvaluationException, "Could not translate expression to MathSAT's format. (Message: " << errorMessage << ")");
                }
				return result;
			}
            
            /*!
             * Translates the given variable to an equivalent expression for MathSAT.
             *
             * @param variable The variable to translate.
             * @return An equivalent term for MathSAT.
             */
            msat_term translateExpression(storm::expressions::Variable const& variable) {
                STORM_LOG_ASSERT(variable.getManager() == this->manager, "Invalid expression for solver.");

                auto const& variableExpressionPair = variableToDeclarationMapping.find(variable);
                if (variableExpressionPair == variableToDeclarationMapping.end()) {
                    return msat_make_constant(env, createVariable(variable));
                }
                return msat_make_constant(env, variableExpressionPair->second);
            }
            
            /*!
             * Retrieves the variable that is associated with the given MathSAT variable declaration.
             *
             * @param msatVariableDeclaration The MathSAT variable declaration.
             * @return The variable associated with the given declaration.
             */
            storm::expressions::Variable const& getVariable(msat_decl msatVariableDeclaration) const {
                auto const& declarationVariablePair = declarationToVariableMapping.find(msatVariableDeclaration);
                STORM_LOG_ASSERT(declarationVariablePair != declarationToVariableMapping.end(), "Unknown variable declaration.");
                return declarationVariablePair->second;
            }

            virtual boost::any visit(storm::expressions::BinaryBooleanFunctionExpression const& expression, boost::any const& data) override {
                msat_term leftResult = boost::any_cast<msat_term>(expression.getFirstOperand()->accept(*this, data));
				msat_term rightResult = boost::any_cast<msat_term>(expression.getSecondOperand()->accept(*this, data));

				switch (expression.getOperatorType()) {
					case storm::expressions::BinaryBooleanFunctionExpression::OperatorType::And:
						return msat_make_and(env, leftResult, rightResult);
					case storm::expressions::BinaryBooleanFunctionExpression::OperatorType::Or:
						return msat_make_or(env, leftResult, rightResult);
					case storm::expressions::BinaryBooleanFunctionExpression::OperatorType::Iff:
						return msat_make_iff(env, leftResult, rightResult);
                    case storm::expressions::BinaryBooleanFunctionExpression::OperatorType::Implies:
                        return msat_make_or(env, msat_make_not(env, leftResult), rightResult);
					default:
                        STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot evaluate expression: unknown boolean binary operator '" << static_cast<uint_fast64_t>(expression.getOperatorType()) << "' in expression " << expression << ".");
				}
			}

			virtual boost::any visit(storm::expressions::BinaryNumericalFunctionExpression const& expression, boost::any const& data) override {
                msat_term leftResult = boost::any_cast<msat_term>(expression.getFirstOperand()->accept(*this, data));
                msat_term rightResult = boost::any_cast<msat_term>(expression.getSecondOperand()->accept(*this, data));

				switch (expression.getOperatorType()) {
					case storm::expressions::BinaryNumericalFunctionExpression::OperatorType::Plus:
						return msat_make_plus(env, leftResult, rightResult);
					case storm::expressions::BinaryNumericalFunctionExpression::OperatorType::Minus:
						return msat_make_plus(env, leftResult, msat_make_times(env, msat_make_number(env, "-1"), rightResult));
					case storm::expressions::BinaryNumericalFunctionExpression::OperatorType::Times:
						return msat_make_times(env, leftResult, rightResult);
					case storm::expressions::BinaryNumericalFunctionExpression::OperatorType::Divide:
                        STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot evaluate expression: unsupported numerical binary operator: '/' (division) in expression.");
					case storm::expressions::BinaryNumericalFunctionExpression::OperatorType::Min:
						return msat_make_term_ite(env, msat_make_leq(env, leftResult, rightResult), leftResult, rightResult);
					case storm::expressions::BinaryNumericalFunctionExpression::OperatorType::Max:
						return msat_make_term_ite(env, msat_make_leq(env, leftResult, rightResult), rightResult, leftResult);
					default:
                        STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot evaluate expression: unknown numerical binary operator '" << static_cast<uint_fast64_t>(expression.getOperatorType()) << "' in expression " << expression << ".");
				}
			}

			virtual boost::any visit(storm::expressions::BinaryRelationExpression const& expression, boost::any const& data) override {
                msat_term leftResult = boost::any_cast<msat_term>(expression.getFirstOperand()->accept(*this, data));
                msat_term rightResult = boost::any_cast<msat_term>(expression.getSecondOperand()->accept(*this, data));

				switch (expression.getRelationType()) {
					case storm::expressions::BinaryRelationExpression::RelationType::Equal:
						if (expression.getFirstOperand()->getType().isBooleanType() && expression.getSecondOperand()->getType().isBooleanType()) {
							return msat_make_iff(env, leftResult, rightResult);
						} else {
							return msat_make_equal(env, leftResult, rightResult);
						}
					case storm::expressions::BinaryRelationExpression::RelationType::NotEqual:
						if (expression.getFirstOperand()->getType().isBooleanType() && expression.getSecondOperand()->getType().isBooleanType()) {
							return msat_make_not(env, msat_make_iff(env, leftResult, rightResult));
						} else {
							return msat_make_not(env, msat_make_equal(env, leftResult, rightResult));
						}
					case storm::expressions::BinaryRelationExpression::RelationType::Less:
						return msat_make_and(env, msat_make_not(env, msat_make_equal(env, leftResult, rightResult)), msat_make_leq(env, leftResult, rightResult));
					case storm::expressions::BinaryRelationExpression::RelationType::LessOrEqual:
						return msat_make_leq(env, leftResult, rightResult);
					case storm::expressions::BinaryRelationExpression::RelationType::Greater:
						return msat_make_not(env, msat_make_leq(env, leftResult, rightResult));
					case storm::expressions::BinaryRelationExpression::RelationType::GreaterOrEqual:
						return msat_make_or(env, msat_make_equal(env, leftResult, rightResult), msat_make_not(env, msat_make_leq(env, leftResult, rightResult)));
					default:
                        STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot evaluate expression: unknown boolean binary operator '" << static_cast<uint_fast64_t>(expression.getRelationType()) << "' in expression " << expression << ".");
				}
			}

			virtual boost::any visit(storm::expressions::IfThenElseExpression const& expression, boost::any const& data) override {
                msat_term conditionResult = boost::any_cast<msat_term>(expression.getCondition()->accept(*this, data));
				msat_term thenResult = boost::any_cast<msat_term>(expression.getThenExpression()->accept(*this, data));
				msat_term elseResult = boost::any_cast<msat_term>(expression.getElseExpression()->accept(*this, data));
                
                // MathSAT does not allow ite with boolean arguments, so we have to encode it ourselves.
                if (expression.getThenExpression()->hasBooleanType() && expression.getElseExpression()->hasBooleanType()) {
                    return msat_make_and(env, msat_make_or(env, msat_make_not(env, conditionResult), thenResult), msat_make_or(env, conditionResult, elseResult));
                } else {
                    return msat_make_term_ite(env, conditionResult, thenResult, elseResult);
                }
			}

			virtual boost::any visit(storm::expressions::BooleanLiteralExpression const& expression, boost::any const&) override {
                return expression.getValue() ? msat_make_true(env) : msat_make_false(env);
			}

			virtual boost::any visit(storm::expressions::RationalLiteralExpression const& expression, boost::any const&) override {
				return msat_make_number(env, std::to_string(expression.getValueAsDouble()).c_str());
			}

			virtual boost::any visit(storm::expressions::IntegerLiteralExpression const& expression, boost::any const&) override {
				return msat_make_number(env, std::to_string(static_cast<int>(expression.getValue())).c_str());
			}

			virtual boost::any visit(storm::expressions::UnaryBooleanFunctionExpression const& expression, boost::any const& data) override {
                msat_term childResult = boost::any_cast<msat_term>(expression.getOperand()->accept(*this, data));

				switch (expression.getOperatorType()) {
					case storm::expressions::UnaryBooleanFunctionExpression::OperatorType::Not:
						return msat_make_not(env, childResult);
						break;
					default:
                    STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot evaluate expression: unknown boolean unary operator: '" << static_cast<uint_fast64_t>(expression.getOperatorType()) << "' in expression " << expression << ".");
				}
			}

			virtual boost::any visit(storm::expressions::UnaryNumericalFunctionExpression const& expression, boost::any const& data) override {
				msat_term childResult = boost::any_cast<msat_term>(expression.getOperand()->accept(*this, data));

				switch (expression.getOperatorType()) {
					case storm::expressions::UnaryNumericalFunctionExpression::OperatorType::Minus:
						return msat_make_times(env, msat_make_number(env, "-1"), childResult);
						break;
                    case storm::expressions::UnaryNumericalFunctionExpression::OperatorType::Floor:
                        return msat_make_floor(env, childResult);
                        break;
                    case storm::expressions::UnaryNumericalFunctionExpression::OperatorType::Ceil:
                        return msat_make_plus(env, msat_make_floor(env, childResult), msat_make_number(env, "1"));
                        break;
					default:
                        STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot evaluate expression: unknown numerical unary operator: '" << static_cast<uint_fast64_t>(expression.getOperatorType()) << "' in expression " << expression << ".");
				}
			}

			virtual boost::any visit(storm::expressions::VariableExpression const& expression, boost::any const&) override {
                return translateExpression(expression.getVariable());
			}

            storm::expressions::Expression translateExpression(msat_term const& term) {
				if (msat_term_is_and(env, term)) {
					return translateExpression(msat_term_get_arg(term, 0)) && translateExpression(msat_term_get_arg(term, 1));
				} else if (msat_term_is_or(env, term)) {
					return translateExpression(msat_term_get_arg(term, 0)) || translateExpression(msat_term_get_arg(term, 1));
				} else if (msat_term_is_iff(env, term)) {
					return storm::expressions::iff(translateExpression(msat_term_get_arg(term, 0)), translateExpression(msat_term_get_arg(term, 1)));
				} else if (msat_term_is_not(env, term)) {
                    return !translateExpression(msat_term_get_arg(term, 0));
				} else if (msat_term_is_plus(env, term)) {
                    return translateExpression(msat_term_get_arg(term, 0)) + translateExpression(msat_term_get_arg(term, 1));
				} else if (msat_term_is_times(env, term)) {
                    return translateExpression(msat_term_get_arg(term, 0)) * translateExpression(msat_term_get_arg(term, 1));
				} else if (msat_term_is_equal(env, term)) {
                    return translateExpression(msat_term_get_arg(term, 0)) == translateExpression(msat_term_get_arg(term, 1));
				} else if (msat_term_is_leq(env, term)) {
                    return translateExpression(msat_term_get_arg(term, 0)) <= translateExpression(msat_term_get_arg(term, 1));
				} else if (msat_term_is_true(env, term)) {
                    return manager.boolean(true);
				} else if (msat_term_is_false(env, term)) {
                    return manager.boolean(false);
				} else if (msat_term_is_constant(env, term)) {
					char* name = msat_decl_get_name(msat_term_get_decl(term));
					std::string nameString(name);
                    storm::expressions::Expression result = manager.getVariableExpression(nameString.substr(0, nameString.find('/')));
					msat_free(name);
                    return result;
				} else if (msat_term_is_number(env, term)) {
					if (msat_is_integer_type(env, msat_term_get_type(term))) {
                        return manager.integer(std::stoll(msat_term_repr(term)));
					} else if (msat_is_rational_type(env, msat_term_get_type(term))) {
                        return manager.rational(std::stod(msat_term_repr(term)));
					}
				}
                
                // If all other cases did not apply, we cannot represent the term in our expression framework.
                char* termAsCString = msat_term_repr(term);
                std::string termString(termAsCString);
                msat_free(termAsCString);
                STORM_LOG_THROW(false, storm::exceptions::ExpressionEvaluationException, "Cannot translate expression: unknown term: '" << termString << "'.");
			}

		private:
            /*!
             * Creates a MathSAT variable for the provided variable.
             *
             * @param variable The variable for which to create a MathSAT counterpart.
             */
            msat_decl createVariable(storm::expressions::Variable const& variable) {
                msat_decl msatDeclaration;
                if (variable.getType().isBooleanType()) {
                    msatDeclaration = msat_declare_function(env, variable.getName().c_str(), msat_get_bool_type(env));
                } else if (variable.getType().isIntegerType()) {
                    msatDeclaration = msat_declare_function(env, variable.getName().c_str(), msat_get_integer_type(env));
                } else if (variable.getType().isBitVectorType()) {
                    msatDeclaration = msat_declare_function(env, variable.getName().c_str(), msat_get_bv_type(env, variable.getType().getWidth()));
                } else if (variable.getType().isRationalType()) {
                    msatDeclaration = msat_declare_function(env, variable.getName().c_str(), msat_get_rational_type(env));
                } else {
                    STORM_LOG_THROW(false, storm::exceptions::InvalidTypeException, "Encountered variable '" << variable.getName() << "' with unknown type while trying to create solver variables.");
                }
                variableToDeclarationMapping.insert(std::make_pair(variable, msatDeclaration));
                declarationToVariableMapping.insert(std::make_pair(msatDeclaration, variable));
                return msatDeclaration;
            }

            // The expression manager to use.
            storm::expressions::ExpressionManager& manager;

            // The MathSAT environment used.
			msat_env& env;

            // A mapping of variable names to their declaration in the MathSAT environment.
            std::unordered_map<storm::expressions::Variable, msat_decl> variableToDeclarationMapping;

            // A mapping from MathSAT variable declaration to our variables.
            std::unordered_map<msat_decl, storm::expressions::Variable> declarationToVariableMapping;
		};
#endif
	} // namespace adapters
} // namespace storm

#endif /* STORM_ADAPTERS_MATHSATEXPRESSIONADAPTER_H_ */
