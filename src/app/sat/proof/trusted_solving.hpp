
#pragma once

#include "app/sat/proof/lrat_checker.hpp"
#include "util/SipHash/siphash.hpp"

/*
Interface for trusted solving.
*/
class TrustedSolving {

// ******************************** INTERFACE ********************************

public:
    TrustedSolving(void (*logFunction)(void*, const char*), void* logger, int nbVars) :
        _log_function(logFunction), _logger(logger),
        _checker(nbVars, _key),
        _siphash(_key) {}

    // Compute the parsed formula's signature. Single use and at one process only.
    void signParsedFormula(const int* literals, int nbLiterals, uint8_t* outSig, int& inOutSigSize) {doSignParsedFormula(literals, nbLiterals, outSig, inOutSigSize);}

    void init(const uint8_t* formulaSignature) {doInit(formulaSignature);}
    void loadLiteral(int lit) {doLoadLiteral(lit);}
    bool endLoading() {return doEndLoading();}

    bool produceClause(unsigned long id, const int* literals, int nbLiterals,
        const unsigned long* hints, int nbHints,
        uint8_t* outSignatureOrNull, int& inOutSigSize) {
        return doProduceClause(id, literals, nbLiterals, hints, nbHints, outSignatureOrNull, inOutSigSize);
    }
    bool importClause(unsigned long id, const int* literals, int nbLiterals,
        const uint8_t* signatureData, int signatureSize) {
        return doImportClause(id, literals, nbLiterals, signatureData, signatureSize);
    }
    bool deleteClauses(const unsigned long* ids, int nbIds) {return doDeleteClauses(ids, nbIds);}

    bool validateUnsat(uint8_t* outSignature, int& inOutSigSize) {return doValidateUnsat(outSignature, inOutSigSize);}

// **************************** END OF INTERFACE *****************************



#define SIG_SIZE_BYTES 16

private:
    static uint8_t _key[SIG_SIZE_BYTES]; // secret

    void (*_log_function)(void*, const char*);
    void* _logger;

    bool _parsed_formula {false};
    uint8_t _formula_signature[SIG_SIZE_BYTES];
    LratChecker _checker;
    SipHash _siphash;


    inline void doSignParsedFormula(const int* lits, unsigned long nbLits, uint8_t* outSig, int& inOutSigSize) {
        if (_parsed_formula) {
            _log_function(_logger, "[ERROR] TS - attempt to sign multiple formulas\n");
            abort();
        }
        // Sign the read data
        if (inOutSigSize < SIG_SIZE_BYTES) {
            abort();
        }
        inOutSigSize = SIG_SIZE_BYTES;
        uint8_t* out = _siphash.reset()
            .update((uint8_t*) lits, sizeof(int) * nbLits)
            .digest();
        copyBytes(outSig, out, SIG_SIZE_BYTES);
        _parsed_formula = true;
    }

    inline void doInit(const uint8_t* formulaSignature) {
        // Store formula signature to validate later after loading
        copyBytes(_formula_signature, formulaSignature, SIG_SIZE_BYTES);
    }

    inline void doLoadLiteral(int lit) {
        _checker.loadLiteral(lit);
    }

    inline bool doEndLoading() {
        uint8_t* sigFromChecker;
        bool ok = _checker.endLoading(sigFromChecker);
        if (!ok) abortWithCheckerError();
		// Check against provided signature
        ok = equalSignatures(sigFromChecker, _formula_signature); 
		if (!ok) {
			_log_function(_logger, "[ERROR] TS - formula signature does not match\n");
			abort();
		}
        return ok;
    }

    inline bool doProduceClause(unsigned long id, const int* literals, int nbLiterals,
        const unsigned long* hints, int nbHints,
        uint8_t* outSignatureOrNull, int& inOutSigSize) {
        
        // forward clause to checker
        bool ok = _checker.addClause(id, literals, nbLiterals, hints, nbHints);
        if (!ok) abortWithCheckerError();
        // compute signature if desired
        if (outSignatureOrNull) {
            computeClauseSignature(id, literals, nbLiterals, outSignatureOrNull, inOutSigSize);
        }
        return ok;
    }

    inline bool doImportClause(unsigned long id, const int* literals, int nbLiterals,
        const uint8_t* signatureData, int signatureSize) {
        
        // verify signature
        int computedSigSize = SIG_SIZE_BYTES;
        uint8_t computedSignature[computedSigSize];
        computeClauseSignature(id, literals, nbLiterals, computedSignature, computedSigSize);
        if (computedSigSize != signatureSize) {
            _log_function(_logger, "[ERROR] TS - supplied clause signature has wrong size\n");
            abort();
        }
        for (int i = 0; i < signatureSize; i++) if (computedSignature[i] != signatureData[i]) {
            _log_function(_logger, "[ERROR] TS - clause signature does not match\n");
            abort();
        }

        // signature verified - forward clause to checker as an axiom
        bool ok = _checker.addAxiomaticClause(id, literals, nbLiterals);
        if (!ok) abortWithCheckerError();
        return ok;
    }

    inline bool doDeleteClauses(const unsigned long* ids, int nbIds) {
        bool ok = _checker.deleteClause(ids, nbIds);
        if (!ok) abortWithCheckerError();
        return ok;
    }

    inline bool doValidateUnsat(uint8_t* outSignature, int& inOutSigSize) {
        bool ok = _checker.validateUnsat();
        if (!ok) abortWithCheckerError();
        _log_function(_logger, "TS - UNSAT checked on-the-fly\n");
        return ok;
    }

    inline void abortWithCheckerError() {
        _log_function(_logger, "[ERROR] TS - LRAT checker error:\n");
        _log_function(_logger, _checker.getErrorMessage());
        abort();
    }



    inline void computeClauseSignature(uint64_t id, const int* lits, int nbLits, uint8_t* out, int& inOutSize) {
        if (inOutSize < SIG_SIZE_BYTES) abort();
        inOutSize = SIG_SIZE_BYTES;
        const uint8_t* hashOut = _siphash.reset()
            .update((uint8_t*) &id, sizeof(uint64_t))
            .update((uint8_t*) lits, nbLits*sizeof(int))
            .update(_key, SIG_SIZE_BYTES)
            .digest();
        copyBytes(out, hashOut, inOutSize);
    }

    inline void computeSignature(const uint8_t* data, int size, uint8_t* out, int& inOutSize) {
        if (inOutSize < SIG_SIZE_BYTES) abort();
        inOutSize = SIG_SIZE_BYTES;
        uint8_t* sipout = _siphash.reset()
            .update(data, size)
            .digest();
        copyBytes(out, sipout, SIG_SIZE_BYTES);
    }

/*
    static inline void computeHmacSignature(const uint8_t* data, int size, uint8_t* out, int& inOutSize, const std::vector<uint8_t>& key) {
        if (inOutSize < 16) abort();
        inOutSize = HMAC::sign_data_128bit(data, size, key.data(), key.size(), out);
    }

    static inline void computeSipHashSignature(const uint8_t* data, int size, uint8_t* out, int& inOutSize, const std::vector<uint8_t> key) {
        if (inOutSize < 16) abort();
        SipHash::sign_data_128bit(data, size, _orig_key.data(), out);
        inOutSize = 16;
    }

    static inline void computeSimpleSignature(const uint8_t* data, int size, uint8_t* out, int& inOutSize, const std::vector<uint8_t>& key) {
        if (inOutSize < sizeof(unsigned long)) abort();
        memcpy(out, key.data(), sizeof(unsigned long));
        for (int i = 0; i < size; i++) hash_combine((unsigned long&) *out, data[i]);
        inOutSize = sizeof(unsigned long);
    }
*/

    void copyBytes(uint8_t* to, const uint8_t* from, size_t nbBytes) {
        for (size_t i = 0; i < nbBytes; i++) {
            to[i] = from[i];
        }
    }

    bool equalSignatures(const uint8_t* left, const uint8_t* right) {
        for (size_t i = 0; i < SIG_SIZE_BYTES; i++) {
            if (left[i] != right[i]) return false;
        }
        return true;
    }

#undef SIG_SIZE_BYTES
};
