/*
 * GmmxxAdapter.h
 *
 *  Created on: 24.12.2012
 *      Author: Christian Dehnert
 */

#ifndef STORM_ADAPTERS_GMMXXADAPTER_H_
#define STORM_ADAPTERS_GMMXXADAPTER_H_

#include <new>

#include "gmm/gmm_matrix.h"

#include "src/storage/SparseMatrix.h"
#include "src/utility/ConversionHelper.h"

#include "log4cplus/logger.h"
#include "log4cplus/loggingmacros.h"

extern log4cplus::Logger logger;

namespace storm {

namespace adapters {

class GmmxxAdapter {
public:
	/*!
	 * Converts a sparse matrix into a sparse matrix in the gmm++ format.
	 * @return A pointer to a row-major sparse matrix in gmm++ format.
	 */
	template<class T>
	static gmm::csr_matrix<T>* toGmmxxSparseMatrix(storm::storage::SparseMatrix<T> const& matrix) {
		uint_fast64_t realNonZeros = matrix.getEntryCount();
		LOG4CPLUS_DEBUG(logger, "Converting matrix with " << realNonZeros << " non-zeros to gmm++ format.");

		// Prepare the resulting matrix.
		gmm::csr_matrix<T>* result = new gmm::csr_matrix<T>(matrix.rowCount, matrix.columnCount);

		// Copy Row Indications
		std::copy(matrix.rowIndications.begin(), matrix.rowIndications.end(), result->jc.begin());
        
        // Copy columns and values.
        std::vector<T> values;
        values.reserve(matrix.getEntryCount());
        std::vector<uint_fast64_t> columns;
        columns.reserve(matrix.getEntryCount());
        
        for (auto const& entry : matrix) {
            columns.emplace_back(entry.first);
            values.emplace_back(entry.second);
        }
        
        std::swap(result->ir, columns);
        std::swap(result->pr, values);
        
		LOG4CPLUS_DEBUG(logger, "Done converting matrix to gmm++ format.");

		return result;
	}

	/*!
	 * Converts a sparse matrix into a sparse matrix in the gmm++ format.
	 * @return A pointer to a row-major sparse matrix in gmm++ format.
	 */
	template<class T>
	static gmm::csr_matrix<T>* toGmmxxSparseMatrix(storm::storage::SparseMatrix<T>&& matrix) {
		uint_fast64_t realNonZeros = matrix.getEntryCount();
        std::cout << "here?!" << std::endl;
		LOG4CPLUS_DEBUG(logger, "Converting matrix with " << realNonZeros << " non-zeros to gmm++ format.");

		// Prepare the resulting matrix.
		gmm::csr_matrix<T>* result = new gmm::csr_matrix<T>(matrix.rowCount, matrix.columnCount);

		typedef unsigned long long IND_TYPE;
        typedef std::vector<IND_TYPE> vectorType_ull_t;
        typedef std::vector<T> vectorType_T_t; 

        // Move Row Indications
        result->jc.~vectorType_ull_t(); // Call Destructor inplace
		new (&result->jc) vectorType_ull_t(std::move(*storm::utility::ConversionHelper::toUnsignedLongLong(&matrix.rowIndications)));
        
        // Copy columns and values.
        std::vector<T> values;
        values.reserve(matrix.getEntryCount());
        std::vector<uint_fast64_t> columns;
        columns.reserve(matrix.getEntryCount());
        
        for (auto const& entry : matrix) {
            columns.emplace_back(entry.first);
            values.emplace_back(entry.second);
        }
        
        std::swap(result->ir, columns);
        std::swap(result->pr, values);

		LOG4CPLUS_DEBUG(logger, "Done converting matrix to gmm++ format.");

		return result;
	}
};

} //namespace adapters

} //namespace storm

#endif /* STORM_ADAPTERS_GMMXXADAPTER_H_ */
