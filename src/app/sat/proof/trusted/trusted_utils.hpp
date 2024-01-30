
#pragma once

#include <cstdlib> // exit
#include <stdio.h> // file I/O
#include <bits/types/FILE.h> // FILE* datatype
#include <unistd.h> // getpid

#define UNLOCKED_IO(fun) fun##_unlocked
//#define UNLOCKED_IO(fun) fun

typedef unsigned long u64;
typedef unsigned char u8;

class TrustedUtils {

public:
    static void doAbortEof() {
        log("end-of-file - terminating");
        ::exit(0);
    }
    static void doAbort() {
        log("ABORT");
        while (true) {}
    }
    static void doAssert(bool exp) {
        if (!exp) doAbort();
    }

    static void log(const char* msg) {
        printf("[TRUSTED_CORE %i] %s\n", getpid(), msg);
    }
    static void log(const char* msg1, const char* msg2) {
        printf("[TRUSTED_CORE %i] %s %s\n", getpid(), msg1, msg2);
    }

    static bool beginsWith(const char* str, const char* prefix) {
        u64 i = 0;
        while (true) {
            if (prefix[i] == '\0') return true;
            if (str[i] == '\0') return prefix[i] == '\0';
            if (str[i] != prefix[i]) return false;
            i++;
        }
    }

    static void readSignature(u8* outSig, FILE* file) {
        u8 dummy[16];
        if (!outSig) outSig = dummy;
        u64 nbRead = UNLOCKED_IO(fread)(outSig, sizeof(int), 4, file);
        if (nbRead < 4) doAbortEof();
    }
    static void writeSignature(const u8* sig, FILE* file) {
        UNLOCKED_IO(fwrite)(sig, sizeof(int), 4, file);
    }

    static u64 readUnsignedLong(FILE* file) {
        u64 u;
        u64 nbRead = UNLOCKED_IO(fread)(&u, sizeof(u64), 1, file);
        if (nbRead < 1) doAbortEof();
        return u;
    }
    static void writeUnsignedLong(u64 u, FILE* file) {
        UNLOCKED_IO(fwrite)(&u, sizeof(u64), 1, file);
    }

    static int readInt(FILE* file) {
        int i;
        u64 nbRead = UNLOCKED_IO(fread)(&i, sizeof(int), 1, file);
        if (nbRead < 1) doAbortEof();
        return i;
    }
    static void readInts(int* data, size_t nbInts, FILE* file) {
        u64 nbRead = UNLOCKED_IO(fread)(data, sizeof(int), nbInts, file);
        if (nbRead < nbInts) doAbortEof();
    }
    static void writeInt(int i, FILE* file) {
        UNLOCKED_IO(fwrite)(&i, sizeof(int), 1, file);
    }
    static void writeInts(const int* data, size_t nbInts, FILE* file) {
        UNLOCKED_IO(fwrite)(data, sizeof(int), nbInts, file);
    }

    static int readChar(FILE* file) {
        int res = UNLOCKED_IO(fgetc)(file);
        if (res == EOF) doAbortEof();
        return res;
    }
    static void writeChar(char c, FILE* file) {
        UNLOCKED_IO(fputc)(c, file);
    }
};
