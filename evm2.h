// disasm3.cpp
// EVM2 Disassembler Class
// Fixed bit-order handling: multi-byte numeric fields are little-endian at the BIT level.
// That is, the first bit read from the stream is the integer's LSB.

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <optional>
#include <stdexcept>

using namespace std;

namespace EVM2
{

enum class Op {
    MOV, LOADCONST, ADD, SUB, DIV, MOD, MUL,
    COMPARE, JUMP, JUMPEQ,
    READ, WRITE, CONSOLEREAD, CONSOLEWRITE,
    CREATETHREAD, JOINTHREAD, HLT, SLEEP,
    CALL, RET, LOCK, UNLOCK, UNKNOWN
};

string opToString(Op o) {
    switch (o) {
        case Op::MOV: return "mov";
        case Op::LOADCONST: return "loadConst";
        case Op::ADD: return "add";
        case Op::SUB: return "sub";
        case Op::DIV: return "div";
        case Op::MOD: return "mod";
        case Op::MUL: return "mul";
        case Op::COMPARE: return "compare";
        case Op::JUMP: return "jump";
        case Op::JUMPEQ: return "jumpEqual";
        case Op::READ: return "read";
        case Op::WRITE: return "write";
        case Op::CONSOLEREAD: return "consoleRead";
        case Op::CONSOLEWRITE: return "consoleWrite";
        case Op::CREATETHREAD: return "createThread";
        case Op::JOINTHREAD: return "joinThread";
        case Op::HLT: return "hlt";
        case Op::SLEEP: return "sleep";
        case Op::CALL: return "call";
        case Op::RET: return "ret";
        case Op::LOCK: return "lock";
        case Op::UNLOCK: return "unlock";
        default: return "unknown";
    }
};

struct Arg {
    typedef uint32_t addr_t;
    enum class Kind { REG, MEM, CONST, ADDR } kind;
    uint8_t reg = 0;
    uint8_t sizeBytes = 0;
    int64_t constValue = 0;
    addr_t addr = 0;
    
    string toString() const {
        switch (kind) {
            case Kind::REG: return "reg" + to_string(reg);
            case Kind::MEM: return to_string(sizeBytes) + "-byte[memReg" + to_string(reg) + "]";
            case Kind::CONST: return "const(" + to_string(constValue) + ")";
            case Kind::ADDR: return "addr(bit:" + to_string(addr) + ")";
        }
        return "?";
    }
};

struct Instruction {
    Op opcode = Op::UNKNOWN;
    uint32_t bitOffset = 0;
    vector<Arg> args;
};

struct FileHeader {
    uint32_t codeSize;
    uint32_t dataSize;
    uint32_t initialDataSize;
};

class BitReader {
private:
    const vector<uint8_t>& bytes;
    uint64_t totalBits;
    uint64_t pos = 0;
    
public:
    BitReader(const vector<uint8_t>& b)
    : bytes(b), totalBits(b.size() * 8ULL) {}
    
    bool eof() const { return pos >= totalBits; }
    
    // Read a single bit (MSB first within the source byte).
    int readBit() {
        if (pos >= totalBits) return -1;
        uint64_t byteIndex = pos / 8;
        unsigned bitIndex = 7 - (pos % 8); // MSB first inside each byte
        int bit = (bytes[byteIndex] >> bitIndex) & 1;
        pos++;
        return bit;
    }
    
    // Read n bits and assemble them as a big-endian bit sequence (first-read bit becomes MSB).
    // Example: reading bits 1,0,1 returns binary 101 (5).
    optional<uint64_t> readBitsBE(unsigned n) {
        if (n == 0) return 0ULL;
        if (pos + n > totalBits) return {};
        uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i) {
            int b = readBit();
            if (b == -1) return {};
            v = (v << 1) | (b & 1);
        }
        return v;
    }
    
    // Read n bits and assemble treating the first-read bit as LSB (little-endian at bit level).
    // Example: stream bits 1,0,1 -> returns binary (LSB..MSB) 1 0 1 -> value 0b101 = 5, same numeric
    // but positional interpretation differs for longer fields. This is what we use for multi-byte fields
    // encoded little-endian at the bit level.
    optional<uint64_t> readBitsLEbits(unsigned n) {
        if (n == 0) return 0ULL;
        if (pos + n > totalBits) return {};
        uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i) {
            int b = readBit();
            if (b == -1) return {};
            v |= (uint64_t(b & 1) << i); // first bit -> bit 0 (LSB)
        }
        return v;
    }
    
    uint64_t getPos() const { return pos; }
};

static const vector<pair<string, Op>> opcodeTable = {
    {"000", Op::MOV},
    {"001", Op::LOADCONST},
    {"010001", Op::ADD},
    {"010010", Op::SUB},
    {"010011", Op::DIV},
    {"010100", Op::MOD},
    {"010101", Op::MUL},
    {"01100", Op::COMPARE},
    {"01101", Op::JUMP},
    {"01110", Op::JUMPEQ},
    {"10000", Op::READ},
    {"10001", Op::WRITE},
    {"10010", Op::CONSOLEREAD},
    {"10011", Op::CONSOLEWRITE},
    {"10100", Op::CREATETHREAD},
    {"10101", Op::JOINTHREAD},
    {"10110", Op::HLT},
    {"10111", Op::SLEEP},
    {"1100", Op::CALL},
    {"1101", Op::RET},
    {"1110", Op::LOCK},
    {"1111", Op::UNLOCK}
};

class Disassembler {
private:
    struct Header {
        char magic[8];
        uint32_t codeSize;
        uint32_t dataSize;
        uint32_t initialDataSize;
    };
    
    static uint32_t readLE32(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
    }
    
    static bool readFile(const string& path, vector<uint8_t>& out) {
        ifstream f(path, ios::binary);
        if (!f) return false;
        f.seekg(0, ios::end);
        size_t sz = (size_t)f.tellg();
        f.seekg(0);
        out.resize(sz);
        f.read((char*)out.data(), sz);
        return true;
    }
    
    static pair<Op, unsigned> readOpcode(BitReader& br) {
        string bits;
        for (unsigned i = 0; i < 6; ++i) {
            int b = br.readBit();
            if (b == -1) break;
            bits.push_back(b ? '1' : '0');
            for (auto &p : opcodeTable) {
                if (p.first == bits) {
                    return {p.second, i + 1};
                }
            }
        }
        return {Op::UNKNOWN, 0};
    }
    
    static optional<Arg> readDataArg(BitReader& br) {
        int first = br.readBit();
        if (first == -1) return {};
        Arg a;
        if (first == 0) {
            // register: next 4 bits are little-endian bit-order for index
            auto idxOpt = br.readBitsLEbits(4);
            if (!idxOpt) return {};
            a.kind = Arg::Kind::REG;
            a.reg = (uint8_t)(*idxOpt & 0xF);
            return a;
        } else {
            // memory: next 2 bits = ss (BE inside these 2 bits), then 4-bit little-endian reg index
            auto ssOpt = br.readBitsLEbits(2);
            if (!ssOpt) return {};
            unsigned ss = (unsigned)*ssOpt;
            unsigned sizeBytes = 1;
            if (ss == 0) sizeBytes = 1;
            else if (ss == 1) sizeBytes = 2;
            else if (ss == 2) sizeBytes = 4;
            else sizeBytes = 8;
            auto regOpt = br.readBitsLEbits(4);
            if (!regOpt) return {};
            a.kind = Arg::Kind::MEM;
            a.sizeBytes = (uint8_t)sizeBytes;
            a.reg = (uint8_t)(*regOpt & 0xF);
            return a;
        }
    }
    
    static vector<Instruction> disassembleCode(const vector<uint8_t>& code) {
        BitReader br(code);
        vector<Instruction> out;
        
        while (br.getPos() < code.size() * 8ULL) {
            uint32_t start = (uint32_t)br.getPos();
            auto [op, consumed] = readOpcode(br);
            if (op == Op::UNKNOWN) break;
            Instruction ins;
            ins.opcode = op;
            ins.bitOffset = start;
            
            switch (op) {
                case Op::MOV: {
                    auto a1 = readDataArg(br);
                    auto a2 = readDataArg(br);
                    if (!a1 || !a2) return out;
                    ins.args.push_back(*a1);
                    ins.args.push_back(*a2);
                    break;
                }
                case Op::LOADCONST: {
                    // read 64 bits little-endian at BIT level
                    auto copt = br.readBitsLEbits(64);
                    if (!copt) return out;
                    int64_t val = (int64_t)(*copt);
                    Arg ac; ac.kind = Arg::Kind::CONST; ac.constValue = val;
                    ins.args.push_back(ac);
                    auto dst = readDataArg(br);
                    if (!dst) return out;
                    ins.args.push_back(*dst);
                    break;
                }
                case Op::ADD: case Op::SUB: case Op::DIV: case Op::MOD: case Op::MUL:
                case Op::COMPARE: {
                    int nargs = (op == Op::MOV ? 2 : 3);
                    for (int i = 0; i < 3; ++i) {
                        // COMPARE and arithmetic use 3 args; MOV handled above
                        if (i >= 3) break;
                    }
                    // for these opcodes read 3 data args (ADD/..../COMPARE), MOV already handled
                    for (int i = 0; i < 3; ++i) {
                        if (op == Op::COMPARE || op == Op::ADD || op == Op::SUB ||
                            op == Op::DIV || op == Op::MOD || op == Op::MUL) {
                            auto a = readDataArg(br);
                            if (!a) return out;
                            ins.args.push_back(*a);
                        }
                    }
                    break;
                }
                case Op::JUMP: {
                    // 32-bit address little-endian at bit level
                    auto aopt = br.readBitsLEbits(32);
                    if (!aopt) return out;
                    Arg ad; ad.kind = Arg::Kind::ADDR; ad.addr = (uint32_t)(*aopt);
                    ins.args.push_back(ad);
                    break;
                }
                case Op::JUMPEQ: {
                    auto aopt = br.readBitsLEbits(32);
                    if (!aopt) return out;
                    Arg ad; ad.kind = Arg::Kind::ADDR; ad.addr = (uint32_t)(*aopt);
                    ins.args.push_back(ad);
                    auto a1 = readDataArg(br);
                    auto a2 = readDataArg(br);
                    if (!a1 || !a2) return out;
                    ins.args.push_back(*a1);
                    ins.args.push_back(*a2);
                    break;
                }
                case Op::READ: {
                    for (int i = 0; i < 4; ++i) {
                        auto a = readDataArg(br);
                        if (!a) return out;
                        ins.args.push_back(*a);
                    }
                    break;
                }
                case Op::WRITE: {
                    for (int i = 0; i < 3; ++i) {
                        auto a = readDataArg(br);
                        if (!a) return out;
                        ins.args.push_back(*a);
                    }
                    break;
                }
                case Op::CREATETHREAD: {
                    // First operand: 32-bit address (little-endian at bit level)
                    auto aopt = br.readBitsLEbits(32);
                    if (!aopt) return out;
                    Arg ad; ad.kind = Arg::Kind::ADDR; ad.addr = (uint32_t)(*aopt);
                    ins.args.push_back(ad);
                    // Second operand: register
                    auto a = readDataArg(br);
                    if (!a) return out;
                    ins.args.push_back(*a);
                    break;
                }
                case Op::CONSOLEREAD:
                case Op::CONSOLEWRITE:
                case Op::JOINTHREAD:
                case Op::SLEEP:
                case Op::LOCK:
                case Op::UNLOCK: {
                    auto a = readDataArg(br);
                    if (!a) return out;
                    ins.args.push_back(*a);
                    break;
                }
                case Op::CALL: {
                    auto aopt = br.readBitsLEbits(32);
                    if (!aopt) return out;
                    Arg ad; ad.kind = Arg::Kind::ADDR; ad.addr = (uint32_t)(*aopt);
                    ins.args.push_back(ad);
                    break;
                }
                case Op::RET:
                case Op::HLT:
                    break;
                    
                default:
                    return out;
            }
            
            out.push_back(ins);
        }
        
        return out;
    }
    
public:
    // Read only the header information from an EVM2 file
    static FileHeader readHeader(const string& filename) {
        vector<uint8_t> file;
        if (!readFile(filename, file)) {
            throw runtime_error("Cannot open file: " + filename);
        }
        
        if (file.size() < sizeof(Header)) {
            throw runtime_error("File too small");
        }
        
        Header hdr;
        memcpy(&hdr, file.data(), sizeof(Header));
        
        if (string(hdr.magic, hdr.magic+8) != "ESET-VM2") {
            throw runtime_error("Invalid magic number");
        }
        
        FileHeader result;
        result.codeSize = readLE32((uint8_t*)&file[8]);
        result.dataSize = readLE32((uint8_t*)&file[12]);
        result.initialDataSize = readLE32((uint8_t*)&file[16]);
        
        return result;
    }
    
    // Constructor that takes a filename and disassembles the file
    explicit Disassembler(const string& filename) {
        vector<uint8_t> file;
        if (!readFile(filename, file)) {
            throw runtime_error("Cannot open file: " + filename);
        }
        
        if (file.size() < sizeof(Header)) {
            throw runtime_error("File too small");
        }
        
        Header hdr;
        memcpy(&hdr, file.data(), sizeof(Header));
        
        if (string(hdr.magic, hdr.magic+8) != "ESET-VM2") {
            throw runtime_error("Invalid magic number");
        }
        
        uint32_t codeSize = readLE32((uint8_t*)&file[8]);
        size_t codeOffset = sizeof(Header);
        if (file.size() < codeOffset + codeSize) {
            throw runtime_error("Truncated file");
        }
        
        vector<uint8_t> code(codeSize);
        memcpy(code.data(), file.data() + codeOffset, codeSize);
        
        instructions = disassembleCode(code);
    }
    
    // Get the disassembled instructions
    const vector<Instruction>& getInstructions() const {
        return instructions;
    }
    
    // Print disassembled instructions to output stream
    void print(ostream& os = cout) const {
        os << "Disassembled " << instructions.size() << " instructions.\n";
        for (size_t i = 0; i < instructions.size(); ++i) {
            const auto& ins = instructions[i];
            os << i << ": bitOffset=" << ins.bitOffset << "  " << opToString(ins.opcode);
            if (!ins.args.empty()) {
                os << "   ";
                for (size_t j = 0; j < ins.args.size(); ++j) {
                    if (j) os << ", ";
                    os << ins.args[j].toString();
                }
            }
            os << "\n";
        }
    }
    
private:
    vector<Instruction> instructions;
};

}
/*
int main(int argc, char** argv) {
    // Example: Read header first to get size information
    try {
        auto header = EVM2::Disassembler::readHeader("crc.evm");
        cout << "File Header Information:\n";
        cout << "  Code Size: " << header.codeSize << " bytes\n";
        cout << "  Data Size: " << header.dataSize << " bytes\n";
        cout << "  Initial Data Size: " << header.initialDataSize << " bytes\n";
        cout << "\n";
    } catch (const exception& e) {
        cerr << "Error reading header: " << e.what() << "\n";
        return 1;
    }
    
    // Now do full disassembly
    EVM2::Disassembler disasm("crc.evm");
    
    // Get the instructions
    const auto& instructions = disasm.getInstructions();
    
    // Print using the built-in print method
    disasm.print();

#if 0
    if (argc < 2) {
        cerr << "Usage: evm2_disasm_fixed <file.evm>\n";
        return 1;
    }

    try {
        // Create disassembler and load file
        EVM2Disassembler disasm(argv[1]);
        
        // Get the instructions
        const auto& instructions = disasm.getInstructions();
        
        // Print using the built-in print method
        disasm.print();
        
        // Or you can work with the instruction vector directly
        // for (const auto& ins : instructions) {
        //     // Process instructions...
        // }
        
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
#endif
    return 0;
}
*/
