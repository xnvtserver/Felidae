#include "BinaryIr.h"
#include "../Symbol.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>

namespace Felidae {
namespace {
constexpr std::array<char, 8> kMagic{'F','E','L','I','R','\0','\0','\0'};
constexpr std::uint32_t kEndian = 0x01020304u;
constexpr std::uint32_t kMaxItems = 1u << 24;
enum class SectionId : std::size_t { Metadata, Constants, Texts, Symbols, Programs, SourceMap, Code, Count };
constexpr std::size_t kSectionCount = static_cast<std::size_t>(SectionId::Count);
constexpr std::size_t kHeaderBytes = 8 + 4 + 4 + 8 + kSectionCount * (4 + 4);

struct SectionHeader {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

template <class T> void writeLe(std::ostream& out, T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U raw = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) out.put(static_cast<char>((raw >> (8 * i)) & 0xff));
    if (!out) throw IrError("cannot write binary IR");
}
template <class T> T readLe(std::istream& in) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U raw = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte = in.get();
        if (byte == EOF) throw IrError("binary IR is truncated");
        raw |= static_cast<U>(static_cast<unsigned char>(byte)) << (8 * i);
    }
    return static_cast<T>(raw);
}
std::uint32_t narrow(std::size_t value, const char* field) {
    if (value > std::numeric_limits<std::uint32_t>::max() || value > kMaxItems)
        throw IrError(std::string("binary IR ") + field + " exceeds its format limit");
    return static_cast<std::uint32_t>(value);
}
void writeString(std::ostream& out, const std::string& value) {
    writeLe<std::uint32_t>(out, narrow(value.size(), "string"));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!out) throw IrError("cannot write binary IR string");
}
std::string readString(std::istream& in) {
    const auto size = readLe<std::uint32_t>(in);
    if (size > kMaxItems) throw IrError("binary IR string exceeds its limit");
    std::string value(size, '\0');
    in.read(value.data(), static_cast<std::streamsize>(size));
    if (!in) throw IrError("binary IR string is truncated");
    return value;
}
std::size_t instructionWidth(const std::vector<IrWord>& words, std::size_t pc);
void rejectLegacy(const FelidaeIr& ir) {
    for (std::size_t pc = 0; pc < ir.words.size();) {
        if (ir.words[pc] >= kIrOpcodeCount) throw IrError("binary IR has an invalid opcode");
        if (static_cast<IrOpcode>(ir.words[pc]) == IrOpcode::ExecuteProgram)
            throw IrError("binary IR forbids legacy ExecuteProgram");
        pc += instructionWidth(ir.words, pc);
    }
}

void writeMetadata(std::ostream& out, const IrProcedureMetadata& m) {
    writeLe<std::uint64_t>(out,m.symbol);
    writeLe<std::uint32_t>(out,narrow(m.positionalParameters.size(),"parameter count")); for(auto x:m.positionalParameters)writeLe<std::uint64_t>(out,x);
    writeLe<std::uint32_t>(out,narrow(m.namedParameters.size(),"named parameter count")); for(auto x:m.namedParameters)writeLe<std::uint64_t>(out,x);
    writeLe<std::uint32_t>(out,narrow(m.registerCount,"register count")); writeLe<std::uint32_t>(out,narrow(m.codeOffset,"code offset")); writeLe<std::uint32_t>(out,narrow(m.codeLength,"code length"));
    writeLe<std::uint32_t>(out,narrow(m.constantOffset,"constant offset")); writeLe<std::uint32_t>(out,narrow(m.constantCount,"constant count")); writeLe<std::uint32_t>(out,narrow(m.textOffset,"text offset")); writeLe<std::uint32_t>(out,narrow(m.textCount,"text count")); writeLe<std::uint32_t>(out,narrow(m.symbolOffset,"symbol offset")); writeLe<std::uint32_t>(out,narrow(m.symbolCount,"symbol count")); writeLe<std::uint32_t>(out,narrow(m.programOffset,"program offset")); writeLe<std::uint32_t>(out,narrow(m.programCount,"program count"));
    writeLe<std::int32_t>(out,m.sourceSpan.startLine);writeLe<std::int32_t>(out,m.sourceSpan.startColumn);writeLe<std::int32_t>(out,m.sourceSpan.endLine);writeLe<std::int32_t>(out,m.sourceSpan.endColumn);writeLe<std::uint8_t>(out,m.deterministicOnly?1:0);
}
IrProcedureMetadata readMetadata(std::istream& in) {
    IrProcedureMetadata m;m.symbol=readLe<std::uint64_t>(in);const auto positional=readLe<std::uint32_t>(in);if(positional>kMaxItems)throw IrError("binary IR parameter count exceeds its limit");for(std::uint32_t i=0;i<positional;++i)m.positionalParameters.push_back(readLe<std::uint64_t>(in));const auto named=readLe<std::uint32_t>(in);if(named>kMaxItems)throw IrError("binary IR named parameter count exceeds its limit");for(std::uint32_t i=0;i<named;++i)m.namedParameters.push_back(readLe<std::uint64_t>(in));m.registerCount=readLe<std::uint32_t>(in);m.codeOffset=readLe<std::uint32_t>(in);m.codeLength=readLe<std::uint32_t>(in);m.constantOffset=readLe<std::uint32_t>(in);m.constantCount=readLe<std::uint32_t>(in);m.textOffset=readLe<std::uint32_t>(in);m.textCount=readLe<std::uint32_t>(in);m.symbolOffset=readLe<std::uint32_t>(in);m.symbolCount=readLe<std::uint32_t>(in);m.programOffset=readLe<std::uint32_t>(in);m.programCount=readLe<std::uint32_t>(in);m.sourceSpan={readLe<std::int32_t>(in),readLe<std::int32_t>(in),readLe<std::int32_t>(in),readLe<std::int32_t>(in)};const auto deterministic=readLe<std::uint8_t>(in);if(deterministic>1)throw IrError("binary IR procedure capability is invalid");m.deterministicOnly=deterministic!=0;return m;
}

std::size_t instructionWidth(const std::vector<IrWord>& words, std::size_t pc) {
    if (pc >= words.size() || words[pc] >= kIrOpcodeCount) throw IrError("linked IR has an invalid opcode");
    const auto op = static_cast<IrOpcode>(words[pc]);
    const auto fixed = [&](std::size_t width) { if (width > words.size() - pc) throw IrError("linked IR instruction is truncated"); return width; };
    switch (op) {
    case IrOpcode::End: return fixed(1); case IrOpcode::Jump: case IrOpcode::ExecuteProgram: return fixed(2);
    case IrOpcode::LoadConst: case IrOpcode::LoadSymbol: case IrOpcode::StoreSymbol: case IrOpcode::Move:
    case IrOpcode::JumpIfFalse: case IrOpcode::CallNative: case IrOpcode::MakeFact: case IrOpcode::Return: return fixed(3);
    case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
    case IrOpcode::GetField: case IrOpcode::SetField: return fixed(4);
    case IrOpcode::Compare: return fixed(5);
    case IrOpcode::Call: case IrOpcode::SemanticEval: case IrOpcode::SsmProcess: case IrOpcode::MakeArray: case IrOpcode::CallNamed: case IrOpcode::MakeMap: {
        fixed(4); const auto stride=(op==IrOpcode::CallNamed||op==IrOpcode::MakeMap)?2u:1u; const auto count=words[pc+3]; if(count>(words.size()-pc-4)/stride)throw IrError("linked IR dynamic instruction is truncated"); return 4+count*stride;
    }
    case IrOpcode::Count: break;
    }
    throw IrError("linked IR opcode is invalid");
}

void rebaseBlockWords(std::vector<IrWord>& words, std::size_t constantBase, std::size_t symbolBase,
                      std::size_t codeBase, bool inverse) {
    const auto adjust = [&](IrWord& value, std::size_t base) {
        if (inverse) { if (value < base) throw IrError("linked IR table reference underflows"); value -= base; }
        else value += base;
    };
    for (std::size_t pc=0; pc<words.size();) {
        const auto op=static_cast<IrOpcode>(words[pc]); const auto width=instructionWidth(words,pc);
        switch(op) {
        case IrOpcode::Jump: adjust(words[pc+1],codeBase); break;
        case IrOpcode::JumpIfFalse: adjust(words[pc+2],codeBase); break;
        case IrOpcode::LoadConst: adjust(words[pc+2],constantBase); break;
        case IrOpcode::LoadSymbol: case IrOpcode::StoreSymbol: case IrOpcode::CallNative: case IrOpcode::MakeFact: adjust(words[pc+2],symbolBase); break;
        case IrOpcode::GetField: case IrOpcode::SetField: adjust(words[pc+3- (op==IrOpcode::SetField ? 1 : 0)],symbolBase); break;
        case IrOpcode::Call: case IrOpcode::SemanticEval: case IrOpcode::SsmProcess: adjust(words[pc+2],symbolBase); break;
        case IrOpcode::CallNamed:
            adjust(words[pc+2],symbolBase); for(std::size_t i=0;i<words[pc+3];++i)if(words[pc+4+2*i]!=0)adjust(words[pc+4+2*i],symbolBase); break;
        case IrOpcode::MakeMap: for(std::size_t i=0;i<words[pc+3];++i)adjust(words[pc+4+2*i],symbolBase); break;
        default: break;
        }
        pc+=width;
    }
}
}

LinkedIrModule linkIrModule(const IrModule& source) {
    verifyIrModule(source);
    LinkedIrModule linked; linked.entryProcedure=source.entryProcedure;
    const auto append = [&](const FelidaeIr& ir, IrProcedureMetadata metadata, LinkedIrModule& target) {
        metadata.registerCount=ir.registerCount; metadata.codeOffset=target.code.size(); metadata.codeLength=ir.words.size();
        metadata.constantOffset=target.constants.size(); metadata.constantCount=ir.constants.size();
        metadata.textOffset=target.texts.size(); metadata.textCount=ir.texts.size();
        metadata.symbolOffset=target.symbols.size(); metadata.symbolCount=ir.symbols.size();
        metadata.programOffset=target.programs.size(); metadata.programCount=ir.programs.size();
        auto constants=ir.constants;
        auto constantKinds=ir.constantKinds;
        if (constantKinds.empty()) constantKinds.assign(constants.size(), IrConstantKind::Number);
        for(std::size_t i=0;i<constants.size();++i) if(constantKinds[i]==IrConstantKind::Text) constants[i]+=metadata.textOffset;
        auto code=ir.words; rebaseBlockWords(code,metadata.constantOffset,metadata.symbolOffset,metadata.codeOffset,false);
        target.constants.insert(target.constants.end(),constants.begin(),constants.end()); target.constantKinds.insert(target.constantKinds.end(),constantKinds.begin(),constantKinds.end()); target.texts.insert(target.texts.end(),ir.texts.begin(),ir.texts.end()); target.symbols.insert(target.symbols.end(),ir.symbols.begin(),ir.symbols.end()); target.programs.insert(target.programs.end(),ir.programs.begin(),ir.programs.end());
        for(auto entry:ir.sourceMap){entry.instructionWord+=metadata.codeOffset;target.sourceMap.push_back(entry);} target.code.insert(target.code.end(),code.begin(),code.end()); return metadata;
    };
    linked.initializer=append(source.ir,IrProcedureMetadata{},linked);
    std::map<IrSymbolRef,const IrProcedure*> sorted;for(const auto& [symbol,p]:source.procedures)sorted.emplace(symbol,&p);
    for(const auto& [symbol,p]:sorted){ IrProcedureMetadata m; m.symbol=symbol;m.positionalParameters=p->positionalParameters;m.namedParameters=p->namedParameters;m.sourceSpan=p->sourceSpan;linked.procedures.push_back(append(p->ir,std::move(m),linked)); }
    return linked;
}

IrModule materializeIrModule(const LinkedIrModule& source) {
    const auto materialize = [&](const IrProcedureMetadata& m) {
        if(m.codeOffset>source.code.size()||m.codeLength>source.code.size()-m.codeOffset||m.constantOffset>source.constants.size()||m.constantCount>source.constants.size()-m.constantOffset||m.textOffset>source.texts.size()||m.textCount>source.texts.size()-m.textOffset||m.symbolOffset>source.symbols.size()||m.symbolCount>source.symbols.size()-m.symbolOffset||m.programOffset>source.programs.size()||m.programCount>source.programs.size()-m.programOffset)throw IrError("linked IR block metadata is outside a module section");
        FelidaeIr ir;ir.registerCount=m.registerCount;ir.words.assign(source.code.begin()+m.codeOffset,source.code.begin()+m.codeOffset+m.codeLength);ir.constants.assign(source.constants.begin()+m.constantOffset,source.constants.begin()+m.constantOffset+m.constantCount);ir.constantKinds.assign(source.constantKinds.begin()+m.constantOffset,source.constantKinds.begin()+m.constantOffset+m.constantCount);ir.texts.assign(source.texts.begin()+m.textOffset,source.texts.begin()+m.textOffset+m.textCount);ir.symbols.assign(source.symbols.begin()+m.symbolOffset,source.symbols.begin()+m.symbolOffset+m.symbolCount);ir.programs.assign(source.programs.begin()+m.programOffset,source.programs.begin()+m.programOffset+m.programCount);
        for(auto entry:source.sourceMap)if(entry.instructionWord>=m.codeOffset&&entry.instructionWord<m.codeOffset+m.codeLength){entry.instructionWord-=m.codeOffset;ir.sourceMap.push_back(entry);} for(std::size_t i=0;i<ir.constants.size();++i)if(!ir.constantKinds.empty()&&ir.constantKinds[i]==IrConstantKind::Text){if(ir.constants[i]<m.textOffset)throw IrError("linked IR text reference underflows");ir.constants[i]-=m.textOffset;} rebaseBlockWords(ir.words,m.constantOffset,m.symbolOffset,m.codeOffset,true);IrVerifier::verify(ir);return ir;
    };
    IrModule module;module.entryProcedure=source.entryProcedure;module.ir=materialize(source.initializer);for(const auto& m:source.procedures){if(m.symbol==0)throw IrError("linked IR procedure symbol is invalid");IrProcedure p;p.ir=materialize(m);p.positionalParameters=m.positionalParameters;p.namedParameters=m.namedParameters;p.sourceSpan=m.sourceSpan;if(!module.procedures.emplace(m.symbol,std::move(p)).second)throw IrError("linked IR has duplicate procedure metadata");}verifyIrModule(module);return module;
}

VmValue RegisterVm::executeMain(const IrModule& module, VmRuntime& runtime,
                                VmValue systemInput) {
    verifyIrModule(module);
    return execute(module.ir, runtime, std::move(systemInput));
}

void verifyIrModule(const IrModule& module) {
    rejectLegacy(module.ir); IrVerifier::verify(module.ir);
    if (module.entryProcedure == 0 || !module.procedures.contains(module.entryProcedure)) throw IrError("IR module entry procedure is invalid");
    for (const auto& [symbol, procedure]:module.procedures) { if(symbol==0) throw IrError("IR module procedure symbol is invalid"); rejectLegacy(procedure.ir); IrVerifier::verify(procedure.ir); }
}

void writeBinaryIr(const std::filesystem::path& path, const IrModule& module) {
    const auto linked=linkIrModule(module);
    const auto makeSection = [](const auto& write) {
        std::ostringstream out(std::ios::out | std::ios::binary);
        write(out);
        return out.str();
    };
    std::array<std::string, kSectionCount> sections;
    sections[static_cast<std::size_t>(SectionId::Metadata)] = makeSection([&](std::ostream& out) {
        writeMetadata(out, linked.initializer);
        for (const auto& metadata : linked.procedures) writeMetadata(out, metadata);
    });
    sections[static_cast<std::size_t>(SectionId::Constants)] = makeSection([&](std::ostream& out) {
        for (std::size_t i = 0; i < linked.constants.size(); ++i) {
            const auto kind = linked.constantKinds.empty() ? IrConstantKind::Number : linked.constantKinds.at(i);
            writeLe<std::uint8_t>(out, static_cast<std::uint8_t>(kind));
            writeLe<std::uint64_t>(out, linked.constants[i]);
        }
    });
    sections[static_cast<std::size_t>(SectionId::Texts)] = makeSection([&](std::ostream& out) {
        for (const auto& text : linked.texts) writeString(out, text);
    });
    sections[static_cast<std::size_t>(SectionId::Symbols)] = makeSection([&](std::ostream& out) {
        for (const auto symbol : linked.symbols) {
            writeLe<std::uint64_t>(out, symbol);
            writeString(out, symbolNameForId(symbol));
        }
    });
    sections[static_cast<std::size_t>(SectionId::Programs)] = makeSection([&](std::ostream& out) {
        for (const auto program : linked.programs) writeLe<std::uint64_t>(out, program);
    });
    sections[static_cast<std::size_t>(SectionId::SourceMap)] = makeSection([&](std::ostream& out) {
        for (const auto& entry : linked.sourceMap) {
            writeLe<std::uint32_t>(out, narrow(entry.instructionWord, "source offset"));
            writeLe<std::int32_t>(out, entry.sourceSpan.startLine);
            writeLe<std::int32_t>(out, entry.sourceSpan.startColumn);
            writeLe<std::int32_t>(out, entry.sourceSpan.endLine);
            writeLe<std::int32_t>(out, entry.sourceSpan.endColumn);
        }
    });
    sections[static_cast<std::size_t>(SectionId::Code)] = makeSection([&](std::ostream& out) {
        for (const auto word : linked.code) writeLe<std::uint64_t>(out, word);
    });

    std::array<SectionHeader, kSectionCount> headers{};
    std::size_t offset = kHeaderBytes;
    const std::array<std::size_t, kSectionCount> counts{
        1 + linked.procedures.size(), linked.constants.size(), linked.texts.size(), linked.symbols.size(),
        linked.programs.size(), linked.sourceMap.size(), linked.code.size()};
    for (std::size_t i = 0; i < kSectionCount; ++i) {
        headers[i] = {narrow(offset, "section offset"), narrow(counts[i], "section count")};
        if (sections[i].size() > std::numeric_limits<std::uint32_t>::max() - offset)
            throw IrError("binary IR file exceeds its format limit");
        offset += sections[i].size();
    }
    std::ofstream out(path, std::ios::binary|std::ios::trunc); if(!out) throw IrError("cannot create binary IR: "+path.string());
    out.write(kMagic.data(),static_cast<std::streamsize>(kMagic.size()));
    writeLe<std::uint32_t>(out,kBinaryIrVersion); writeLe<std::uint32_t>(out,kEndian);
    writeLe<std::uint64_t>(out,linked.entryProcedure);
    for (const auto& header : headers) { writeLe<std::uint32_t>(out, header.offset); writeLe<std::uint32_t>(out, header.count); }
    for (const auto& section : sections) out.write(section.data(), static_cast<std::streamsize>(section.size()));
    if (!out) throw IrError("cannot write binary IR");
}

IrModule loadBinaryIr(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); if(!in) throw IrError("cannot open binary IR: "+path.string());
    in.seekg(0, std::ios::end); const auto end = in.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) < kHeaderBytes) throw IrError("binary IR is truncated");
    const auto fileSize = static_cast<std::size_t>(end); in.seekg(0);
    std::array<char,8> magic{}; in.read(magic.data(),8); if(!in||magic!=kMagic)throw IrError("binary IR magic is invalid");
    if(readLe<std::uint32_t>(in)!=kBinaryIrVersion)throw IrError("binary IR version is unsupported");
    if(readLe<std::uint32_t>(in)!=kEndian)throw IrError("binary IR endian marker is invalid");
    LinkedIrModule linked; linked.entryProcedure=readLe<std::uint64_t>(in);
    std::array<SectionHeader, kSectionCount> headers{};
    for (auto& header : headers) {
        header.offset = readLe<std::uint32_t>(in); header.count = readLe<std::uint32_t>(in);
        if (header.count > kMaxItems) throw IrError("binary IR section count exceeds its limit");
    }
    if (headers[0].offset != kHeaderBytes || headers[0].count == 0) throw IrError("binary IR metadata section is invalid");
    for (std::size_t i = 0; i < kSectionCount; ++i) {
        if (headers[i].offset < kHeaderBytes || headers[i].offset > fileSize) throw IrError("binary IR section offset is invalid");
        if (i != 0 && headers[i].offset < headers[i - 1].offset) throw IrError("binary IR section order is invalid");
    }
    const auto readSection = [&](std::size_t index) {
        const auto begin = static_cast<std::size_t>(headers[index].offset);
        const auto finish = index + 1 == kSectionCount ? fileSize : static_cast<std::size_t>(headers[index + 1].offset);
        std::string bytes(finish - begin, '\0'); in.seekg(static_cast<std::streamoff>(begin)); in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!in && !bytes.empty()) throw IrError("binary IR section is truncated");
        return std::istringstream(std::move(bytes), std::ios::in | std::ios::binary);
    };
    auto metadata = readSection(static_cast<std::size_t>(SectionId::Metadata));
    linked.initializer=readMetadata(metadata); if(linked.initializer.symbol!=0)throw IrError("binary IR initializer symbol is invalid");
    for(std::uint32_t i=1;i<headers[0].count;++i) linked.procedures.push_back(readMetadata(metadata));
    if (metadata.peek()!=EOF) throw IrError("binary IR metadata section size is invalid");
    auto constants = readSection(static_cast<std::size_t>(SectionId::Constants));
    for(std::uint32_t i=0;i<headers[1].count;++i){const auto kind=readLe<std::uint8_t>(constants);if(kind>static_cast<std::uint8_t>(IrConstantKind::Text))throw IrError("binary IR constant kind is invalid");linked.constantKinds.push_back(static_cast<IrConstantKind>(kind));linked.constants.push_back(readLe<std::uint64_t>(constants));} if(constants.peek()!=EOF)throw IrError("binary IR constant section size is invalid");
    auto texts = readSection(static_cast<std::size_t>(SectionId::Texts)); for(std::uint32_t i=0;i<headers[2].count;++i)linked.texts.push_back(readString(texts)); if(texts.peek()!=EOF)throw IrError("binary IR text section size is invalid");
    auto symbols = readSection(static_cast<std::size_t>(SectionId::Symbols)); for(std::uint32_t i=0;i<headers[3].count;++i){const auto id=readLe<std::uint64_t>(symbols);const auto name=readString(symbols);if(name.empty()||symbolIdForName(name)!=id)throw IrError("binary IR symbol spelling does not match its ID");linked.symbols.push_back(id);}if(symbols.peek()!=EOF)throw IrError("binary IR symbol section size is invalid");
    auto programs = readSection(static_cast<std::size_t>(SectionId::Programs)); for(std::uint32_t i=0;i<headers[4].count;++i)linked.programs.push_back(readLe<std::uint64_t>(programs));if(programs.peek()!=EOF)throw IrError("binary IR program section size is invalid");
    auto sourceMap = readSection(static_cast<std::size_t>(SectionId::SourceMap)); for(std::uint32_t i=0;i<headers[5].count;++i){IrSourceMapEntry entry;entry.instructionWord=readLe<std::uint32_t>(sourceMap);entry.sourceSpan={readLe<std::int32_t>(sourceMap),readLe<std::int32_t>(sourceMap),readLe<std::int32_t>(sourceMap),readLe<std::int32_t>(sourceMap)};linked.sourceMap.push_back(entry);}if(sourceMap.peek()!=EOF)throw IrError("binary IR source-map section size is invalid");
    auto code = readSection(static_cast<std::size_t>(SectionId::Code)); for(std::uint32_t i=0;i<headers[6].count;++i)linked.code.push_back(readLe<std::uint64_t>(code));if(code.peek()!=EOF)throw IrError("binary IR code section size is invalid");
    return materializeIrModule(linked);
}
} // namespace Felidae
