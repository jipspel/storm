
#ifndef STORM_H
#define	STORM_H

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdio>
#include <sstream>
#include <memory>
#include <src/storage/ModelFormulasPair.h>

#include "initialize.h"

#include "storm-config.h"



// Headers that provide auxiliary functionality.
#include "src/settings/SettingsManager.h"


#include "src/settings/modules/BisimulationSettings.h"
#include "src/settings/modules/ParametricSettings.h"


// Formula headers.
#include "src/logic/Formulas.h"

// Model headers.
#include "src/models/ModelBase.h"
#include "src/models/sparse/Model.h"
#include "src/models/sparse/StandardRewardModel.h"
#include "src/models/sparse/MarkovAutomaton.h"
#include "src/models/symbolic/Model.h"
#include "src/models/symbolic/StandardRewardModel.h"

#include "src/storage/dd/Add.h"
#include "src/storage/dd/Bdd.h"

#include "src/parser/AutoParser.h"

// Headers of builders.
#include "src/builder/ExplicitPrismModelBuilder.h"
#include "src/builder/DdPrismModelBuilder.h"

// Headers for model processing.
#include "src/storage/bisimulation/DeterministicModelBisimulationDecomposition.h"
#include "src/storage/bisimulation/NondeterministicModelBisimulationDecomposition.h"
#include "src/storage/ModelFormulasPair.h"

// Headers for model checking.
#include "src/modelchecker/prctl/SparseDtmcPrctlModelChecker.h"
#include "src/modelchecker/prctl/SparseMdpPrctlModelChecker.h"
#include "src/modelchecker/prctl/HybridDtmcPrctlModelChecker.h"
#include "src/modelchecker/prctl/HybridMdpPrctlModelChecker.h"
#include "src/modelchecker/prctl/SymbolicDtmcPrctlModelChecker.h"
#include "src/modelchecker/prctl/SymbolicMdpPrctlModelChecker.h"
#include "src/modelchecker/reachability/SparseDtmcEliminationModelChecker.h"
#include "src/modelchecker/csl/SparseCtmcCslModelChecker.h"
#include "src/modelchecker/csl/HybridCtmcCslModelChecker.h"
#include "src/modelchecker/results/ExplicitQualitativeCheckResult.h"
#include "src/modelchecker/results/SymbolicQualitativeCheckResult.h"

// Headers for counterexample generation.
#include "src/counterexamples/MILPMinimalLabelSetGenerator.h"
#include "src/counterexamples/SMTMinimalCommandSetGenerator.h"

// Headers related to exception handling.
#include "src/exceptions/InvalidStateException.h"
#include "src/exceptions/InvalidArgumentException.h"
#include "src/exceptions/InvalidSettingsException.h"
#include "src/exceptions/InvalidTypeException.h"
#include "src/exceptions/NotImplementedException.h"

namespace storm {

    template<typename ValueType>
    std::shared_ptr<storm::models::sparse::Model<ValueType>> buildExplicitModel(std::string const& transitionsFile, std::string const& labelingFile, boost::optional<std::string> const& stateRewardsFile = boost::optional<std::string>(), boost::optional<std::string> const& transitionRewardsFile = boost::optional<std::string>(), boost::optional<std::string> const& choiceLabelingFile = boost::optional<std::string>()) {
        return storm::parser::AutoParser<>::parseModel(transitionsFile, labelingFile, stateRewardsFile ? stateRewardsFile.get() : "", transitionRewardsFile ? transitionRewardsFile.get() : "", choiceLabelingFile ? choiceLabelingFile.get() : "" );
    }
            
    storm::prism::Program parseProgram(std::string const& path);
    std::vector<std::shared_ptr<storm::logic::Formula>> parseFormulasForExplicit(std::string const& inputString);
    std::vector<std::shared_ptr<storm::logic::Formula>> parseFormulasForProgram(std::string const& inputString, storm::prism::Program const& program);


    template<typename ValueType, storm::dd::DdType LibraryType = storm::dd::DdType::CUDD>
    storm::storage::ModelFormulasPair buildSymbolicModel(storm::prism::Program const& program, std::vector<std::shared_ptr<storm::logic::Formula>> const& formulas) {
        storm::storage::ModelFormulasPair result;
        storm::prism::Program translatedProgram;

        storm::settings::modules::GeneralSettings settings = storm::settings::generalSettings();

        // Get the string that assigns values to the unknown currently undefined constants in the model.
        std::string constants = settings.getConstantDefinitionString();

        // Customize and perform model-building.
        if (settings.getEngine() == storm::settings::modules::GeneralSettings::Engine::Sparse) {
            typename storm::builder::ExplicitPrismModelBuilder<ValueType, storm::models::sparse::StandardRewardModel<ValueType>>::Options options;
            options = typename storm::builder::ExplicitPrismModelBuilder<ValueType, storm::models::sparse::StandardRewardModel<ValueType>>::Options(formulas);
            options.addConstantDefinitionsFromString(program, constants);

            // Generate command labels if we are going to build a counterexample later.
            if (storm::settings::counterexampleGeneratorSettings().isMinimalCommandSetGenerationSet()) {
                options.buildCommandLabels = true;
            }

            storm::builder::ExplicitPrismModelBuilder<ValueType> builder;
            result.model = builder.translateProgram(program, options);
            translatedProgram = builder.getTranslatedProgram();
        } else if (settings.getEngine() == storm::settings::modules::GeneralSettings::Engine::Dd || settings.getEngine() == storm::settings::modules::GeneralSettings::Engine::Hybrid) {
            typename storm::builder::DdPrismModelBuilder<LibraryType>::Options options;
            options = typename storm::builder::DdPrismModelBuilder<LibraryType>::Options(formulas);
            options.addConstantDefinitionsFromString(program, constants);

            storm::builder::DdPrismModelBuilder<LibraryType> builder;
            result.model = builder.translateProgram(program, options);
            translatedProgram = builder.getTranslatedProgram();
        }
        // There may be constants of the model appearing in the formulas, so we replace all their occurrences
        // by their definitions in the translated program.

        // Start by building a mapping from constants of the (translated) model to their defining expressions.
        std::map<storm::expressions::Variable, storm::expressions::Expression> constantSubstitution;
        for (auto const& constant : translatedProgram.getConstants()) {
            if (constant.isDefined()) {
                constantSubstitution.emplace(constant.getExpressionVariable(), constant.getExpression());
            }
        }

        for (auto const& formula : formulas) {
            result.formulas.emplace_back(formula->substitute(constantSubstitution));
        }
        return result;
    }
    
    template<typename ModelType>
    std::shared_ptr<ModelType> performDeterministicSparseBisimulationMinimization(std::shared_ptr<ModelType> model, std::vector<std::shared_ptr<storm::logic::Formula>> const& formulas, storm::storage::BisimulationType type) {
        std::cout << "Performing bisimulation minimization... ";
        typename storm::storage::DeterministicModelBisimulationDecomposition<ModelType>::Options options;
        if (!formulas.empty()) {
            options = typename storm::storage::DeterministicModelBisimulationDecomposition<ModelType>::Options(*model, formulas);
        }
        options.setType(type);
        
        storm::storage::DeterministicModelBisimulationDecomposition<ModelType> bisimulationDecomposition(*model, options);
        bisimulationDecomposition.computeBisimulationDecomposition();
        model = bisimulationDecomposition.getQuotient();
        std::cout << "done." << std::endl << std::endl;
        return model;
    }
    
    template<typename ModelType>
    std::shared_ptr<ModelType> performNondeterministicSparseBisimulationMinimization(std::shared_ptr<ModelType> model, std::vector<std::shared_ptr<storm::logic::Formula>> const& formulas, storm::storage::BisimulationType type) {
        std::cout << "Performing bisimulation minimization... ";
        typename storm::storage::DeterministicModelBisimulationDecomposition<ModelType>::Options options;
        if (!formulas.empty()) {
            options = typename storm::storage::NondeterministicModelBisimulationDecomposition<ModelType>::Options(*model, formulas);
        }
        options.setType(type);
        
        
        storm::storage::NondeterministicModelBisimulationDecomposition<ModelType> bisimulationDecomposition(*model, options);
        bisimulationDecomposition.computeBisimulationDecomposition();
        model = bisimulationDecomposition.getQuotient();
        std::cout << "done." << std::endl << std::endl;
        return model;
    }
    
    template<typename ModelType>
    std::shared_ptr<storm::models::sparse::Model<typename ModelType::ValueType>> performBisimulationMinimization(std::shared_ptr<storm::models::sparse::Model<typename ModelType::ValueType>> const& model, std::vector<std::shared_ptr<storm::logic::Formula>> const& formulas, storm::storage::BisimulationType type) {
        using ValueType = typename ModelType::ValueType;
        
        STORM_LOG_THROW(model->isOfType(storm::models::ModelType::Dtmc) || model->isOfType(storm::models::ModelType::Ctmc) || model->isOfType(storm::models::ModelType::Mdp), storm::exceptions::InvalidSettingsException, "Bisimulation minimization is currently only available for DTMCs, CTMCs and MDPs.");
        model->reduceToStateBasedRewards();

        if (model->isOfType(storm::models::ModelType::Dtmc)) {
            return performDeterministicSparseBisimulationMinimization<storm::models::sparse::Dtmc<ValueType>>(model->template as<storm::models::sparse::Dtmc<ValueType>>(), formulas, type);
        } else if (model->isOfType(storm::models::ModelType::Ctmc)) {
            return performDeterministicSparseBisimulationMinimization<storm::models::sparse::Ctmc<ValueType>>(model->template as<storm::models::sparse::Ctmc<ValueType>>(), formulas, type);
        } else {
            return performNondeterministicSparseBisimulationMinimization<storm::models::sparse::Mdp<ValueType>>(model->template as<storm::models::sparse::Mdp<ValueType>>(), formulas, type);
        }       
    }
        
    template<typename ModelType>
    std::shared_ptr<storm::models::sparse::Model<typename ModelType::ValueType>> performBisimulationMinimization(std::shared_ptr<storm::models::sparse::Model<typename ModelType::ValueType>> const& model, std::shared_ptr<storm::logic::Formula> const& formula, storm::storage::BisimulationType type) {
        std::vector<std::shared_ptr<storm::logic::Formula>> formulas = { formula };
        return performBisimulationMinimization<ModelType>(model, formulas , type);
    }
    
    
    template<typename ModelType>
    std::shared_ptr<storm::models::ModelBase> preprocessModel(std::shared_ptr<storm::models::ModelBase> model, std::vector<std::shared_ptr<storm::logic::Formula>> const& formulas) {
        if (model->isSparseModel() && storm::settings::generalSettings().isBisimulationSet()) {
            storm::storage::BisimulationType bisimType = storm::storage::BisimulationType::Strong;
            if (storm::settings::bisimulationSettings().isWeakBisimulationSet()) {
                bisimType = storm::storage::BisimulationType::Weak;
            }
            
            STORM_LOG_THROW(model->isSparseModel(), storm::exceptions::InvalidSettingsException, "Bisimulation minimization is currently only available for sparse models.");
            return performBisimulationMinimization<ModelType>(model->template as<storm::models::sparse::Model<typename ModelType::ValueType>>(), formulas, bisimType);
        }
        return model;
    }
    
    
    template<typename ValueType>
    void generateCounterexample(storm::prism::Program const& program, std::shared_ptr<storm::models::sparse::Model<ValueType>> model, std::shared_ptr<storm::logic::Formula> const& formula) {
        if (storm::settings::counterexampleGeneratorSettings().isMinimalCommandSetGenerationSet()) {
            STORM_LOG_THROW(model->getType() == storm::models::ModelType::Mdp, storm::exceptions::InvalidTypeException, "Minimal command set generation is only available for MDPs.");
            STORM_LOG_THROW(storm::settings::generalSettings().isSymbolicSet(), storm::exceptions::InvalidSettingsException, "Minimal command set generation is only available for symbolic models.");

            std::shared_ptr<storm::models::sparse::Mdp<ValueType>> mdp = model->template as<storm::models::sparse::Mdp<ValueType>>();

            // Determine whether we are required to use the MILP-version or the SAT-version.
            bool useMILP = storm::settings::counterexampleGeneratorSettings().isUseMilpBasedMinimalCommandSetGenerationSet();

            if (useMILP) {
                storm::counterexamples::MILPMinimalLabelSetGenerator<ValueType>::computeCounterexample(program, *mdp, formula);
            } else {
                storm::counterexamples::SMTMinimalCommandSetGenerator<ValueType>::computeCounterexample(program, storm::settings::generalSettings().getConstantDefinitionString(), *mdp, formula);
            }

        } else {
            STORM_LOG_THROW(false, storm::exceptions::InvalidSettingsException, "No suitable counterexample representation selected.");
        }
    }
            
#ifdef STORM_HAVE_CARL
    template<>
    inline void generateCounterexample(storm::prism::Program const& program, std::shared_ptr<storm::models::sparse::Model<storm::RationalFunction>> model, std::shared_ptr<storm::logic::Formula> const& formula) {
        STORM_LOG_THROW(false, storm::exceptions::InvalidSettingsException, "Unable to generate counterexample for parametric model.");
    }
#endif

    template<typename ValueType, storm::dd::DdType DdType>
    std::unique_ptr<storm::modelchecker::CheckResult> verifyModel(std::shared_ptr<storm::models::ModelBase> model, std::shared_ptr<storm::logic::Formula> const& formula) {
        storm::settings::modules::GeneralSettings const& settings = storm::settings::generalSettings();
        switch(settings.getEngine()) {
            case storm::settings::modules::GeneralSettings::Engine::Sparse: {
                std::shared_ptr<storm::models::sparse::Model<ValueType>> sparseModel = model->template as<storm::models::sparse::Model<ValueType>>();
                STORM_LOG_THROW(sparseModel != nullptr, storm::exceptions::InvalidArgumentException, "Sparse engine requires a sparse input model");
                return verifySparseModel(sparseModel, formula);
            }
            case storm::settings::modules::GeneralSettings::Engine::Hybrid: {
                std::shared_ptr<storm::models::symbolic::Model<DdType>> ddModel = model->template as<storm::models::symbolic::Model<DdType>>();
                STORM_LOG_THROW(ddModel != nullptr, storm::exceptions::InvalidArgumentException, "Hybrid engine requires a dd input model");
                return verifySymbolicModelWithHybridEngine(ddModel, formula);
            }
            case storm::settings::modules::GeneralSettings::Engine::Dd: {
                std::shared_ptr<storm::models::symbolic::Model<DdType>> ddModel = model->template as<storm::models::symbolic::Model<DdType>>();
                STORM_LOG_THROW(ddModel != nullptr, storm::exceptions::InvalidArgumentException, "Dd engine requires a dd input model");
                return verifySymbolicModelWithDdEngine(ddModel, formula);
            }
            case storm::settings::modules::GeneralSettings::Engine::AbstractionRefinement: {
                STORM_LOG_ASSERT(false, "This position should not be reached, as at this point no model has been built.");
            }
        }
    }

    template<typename ValueType>
    std::unique_ptr<storm::modelchecker::CheckResult> verifySparseModel(std::shared_ptr<storm::models::sparse::Model<ValueType>> model, std::shared_ptr<storm::logic::Formula> const& formula) {

        std::unique_ptr<storm::modelchecker::CheckResult> result;
        if (model->getType() == storm::models::ModelType::Dtmc) {
            std::shared_ptr<storm::models::sparse::Dtmc<ValueType>> dtmc = model->template as<storm::models::sparse::Dtmc<ValueType>>();
            storm::modelchecker::SparseDtmcPrctlModelChecker<storm::models::sparse::Dtmc<ValueType>> modelchecker(*dtmc);
            if (modelchecker.canHandle(*formula)) {
                result = modelchecker.check(*formula);
            } else {
                storm::modelchecker::SparseDtmcEliminationModelChecker<storm::models::sparse::Dtmc<ValueType>> modelchecker2(*dtmc);
                if (modelchecker2.canHandle(*formula)) {
                    result = modelchecker2.check(*formula);
                }
            }
        } else if (model->getType() == storm::models::ModelType::Mdp) {
            std::shared_ptr<storm::models::sparse::Mdp<ValueType>> mdp = model->template as<storm::models::sparse::Mdp<ValueType>>();
#ifdef STORM_HAVE_CUDA
            if (settings.isCudaSet()) {
                    storm::modelchecker::TopologicalValueIterationMdpPrctlModelChecker<ValueType> modelchecker(*mdp);
                    result = modelchecker.check(*formula);
                } else {
                    storm::modelchecker::SparseMdpPrctlModelChecker<storm::models::sparse::Mdp<ValueType>> modelchecker(*mdp);
                    result = modelchecker.check(*formula);
                }
#else
            storm::modelchecker::SparseMdpPrctlModelChecker<storm::models::sparse::Mdp<ValueType>> modelchecker(*mdp);
            result = modelchecker.check(*formula);
#endif
        } else if (model->getType() == storm::models::ModelType::Ctmc) {
            std::shared_ptr<storm::models::sparse::Ctmc<ValueType>> ctmc = model->template as<storm::models::sparse::Ctmc<ValueType>>();

            storm::modelchecker::SparseCtmcCslModelChecker<storm::models::sparse::Ctmc<ValueType>> modelchecker(*ctmc);
            result = modelchecker.check(*formula);
        }
        return result;

    }
            
#ifdef STORM_HAVE_CARL
    inline void exportParametricResultToFile(storm::RationalFunction const& result, storm::models::sparse::Dtmc<storm::RationalFunction>::ConstraintCollector const& constraintCollector, std::string const& path) {
        std::ofstream filestream;
        filestream.open(path);
        // TODO: add checks.
        filestream << "!Parameters: ";
        std::set<storm::Variable> vars = result.gatherVariables();
        std::copy(vars.begin(), vars.end(), std::ostream_iterator<storm::Variable>(filestream, ", "));
        filestream << std::endl;
        filestream << "!Result: " << result << std::endl;
        filestream << "!Well-formed Constraints: " << std::endl;
        std::copy(constraintCollector.getWellformedConstraints().begin(), constraintCollector.getWellformedConstraints().end(), std::ostream_iterator<storm::ArithConstraint<storm::RationalFunction>>(filestream, "\n"));
        filestream << "!Graph-preserving Constraints: " << std::endl;
        std::copy(constraintCollector.getGraphPreservingConstraints().begin(), constraintCollector.getGraphPreservingConstraints().end(), std::ostream_iterator<storm::ArithConstraint<storm::RationalFunction>>(filestream, "\n"));
        filestream.close();
    }

    template<>
    inline std::unique_ptr<storm::modelchecker::CheckResult> verifySparseModel(std::shared_ptr<storm::models::sparse::Model<storm::RationalFunction>> model, std::shared_ptr<storm::logic::Formula> const& formula) {
        std::unique_ptr<storm::modelchecker::CheckResult> result;
        std::shared_ptr<storm::models::sparse::Dtmc<storm::RationalFunction>> dtmc = model->template as<storm::models::sparse::Dtmc<storm::RationalFunction>>();

        storm::modelchecker::SparseDtmcEliminationModelChecker<storm::models::sparse::Dtmc<storm::RationalFunction>> modelchecker(*dtmc);
        if (modelchecker.canHandle(*formula)) {
            result = modelchecker.check(*formula);
        } else {
            STORM_LOG_THROW(false, storm::exceptions::InvalidSettingsException, "The parametric engine currently does not support this property.");
        }
        return result;
    }
            
#endif

    template<storm::dd::DdType DdType>
    std::unique_ptr<storm::modelchecker::CheckResult> verifySymbolicModelWithHybridEngine(std::shared_ptr<storm::models::symbolic::Model<DdType>> model, std::shared_ptr<storm::logic::Formula> const& formula) {
        std::unique_ptr<storm::modelchecker::CheckResult> result;
        if (model->getType() == storm::models::ModelType::Dtmc) {
            std::shared_ptr<storm::models::symbolic::Dtmc<DdType>> dtmc = model->template as<storm::models::symbolic::Dtmc<DdType>>();
            storm::modelchecker::HybridDtmcPrctlModelChecker<DdType, double> modelchecker(*dtmc);
            if (modelchecker.canHandle(*formula)) {
                result = modelchecker.check(*formula);
            }
        } else if (model->getType() == storm::models::ModelType::Ctmc) {
            std::shared_ptr<storm::models::symbolic::Ctmc<DdType>> ctmc = model->template as<storm::models::symbolic::Ctmc<DdType>>();
            storm::modelchecker::HybridCtmcCslModelChecker<DdType, double> modelchecker(*ctmc);
            if (modelchecker.canHandle(*formula)) {
                result = modelchecker.check(*formula);
            }
        } else if (model->getType() == storm::models::ModelType::Mdp) {
            std::shared_ptr<storm::models::symbolic::Mdp<DdType>> mdp = model->template as<storm::models::symbolic::Mdp<DdType>>();
            storm::modelchecker::HybridMdpPrctlModelChecker<DdType, double> modelchecker(*mdp);
            if (modelchecker.canHandle(*formula)) {
                result = modelchecker.check(*formula);
            }
        } else {
            STORM_LOG_THROW(false, storm::exceptions::NotImplementedException, "This functionality is not yet implemented.");
        }
        return result;
    }


    template<storm::dd::DdType DdType>
    std::unique_ptr<storm::modelchecker::CheckResult> verifySymbolicModelWithDdEngine(std::shared_ptr<storm::models::symbolic::Model<DdType>> model, std::shared_ptr<storm::logic::Formula> const& formula) {
        std::unique_ptr<storm::modelchecker::CheckResult> result;
        if (model->getType() == storm::models::ModelType::Dtmc) {
            std::shared_ptr<storm::models::symbolic::Dtmc<DdType>> dtmc = model->template as<storm::models::symbolic::Dtmc<DdType>>();
            storm::modelchecker::SymbolicDtmcPrctlModelChecker<DdType, double> modelchecker(*dtmc);
            if (modelchecker.canHandle(*formula)) {
                result = modelchecker.check(*formula);
            }
        } else if (model->getType() == storm::models::ModelType::Mdp) {
            std::shared_ptr<storm::models::symbolic::Mdp<DdType>> mdp = model->template as<storm::models::symbolic::Mdp<DdType>>();
            storm::modelchecker::SymbolicMdpPrctlModelChecker<DdType, double> modelchecker(*mdp);
            if (modelchecker.canHandle(*formula)) {
                result = modelchecker.check(*formula);
            }
        } else {
            STORM_LOG_THROW(false, storm::exceptions::NotImplementedException, "This functionality is not yet implemented.");
        }
        return result;
    }

    template<typename ValueType>
    void exportMatrixToFile(std::shared_ptr<storm::models::sparse::Model<ValueType>> model, std::string const& filepath) {
        STORM_LOG_THROW(model->getType() != storm::models::ModelType::Ctmc, storm::exceptions::NotImplementedException, "This functionality is not yet implemented." );
        std::ofstream ofs;
        ofs.open (filepath, std::ofstream::out);
        model->getTransitionMatrix().printAsMatlabMatrix(ofs);
        ofs.close();
    }
        
}

#endif	/* STORM_H */

