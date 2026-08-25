#include "BinaryIsa.h"

#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Felidae {
namespace {

constexpr std::array<char,8> kMagic{'F','E','L','B','I','N','\0','\0'};
constexpr std::uint32_t kEndian=0x01020304u;
constexpr std::uint32_t kMaximumItems=1u<<24;
constexpr std::uint64_t kMaximumBytes=256ull*1024ull*1024ull;
constexpr std::size_t kHeaderBytes=8+4+4+4+2+2+4+4+4;

template<class T>void writeLe(std::ostream& out,T value){
    static_assert(std::is_integral_v<T>);using U=std::make_unsigned_t<T>;const auto raw=static_cast<U>(value);
    for(std::size_t i=0;i<sizeof(T);++i)out.put(static_cast<char>((raw>>(i*8u))&0xffu));
    if(!out)throw IrError("cannot write FELBIN");
}
template<class T>T readLe(std::istream& in){
    static_assert(std::is_integral_v<T>);using U=std::make_unsigned_t<T>;U raw=0;
    for(std::size_t i=0;i<sizeof(T);++i){const auto byte=in.get();if(byte==EOF)throw IrError("FELBIN is truncated");raw|=static_cast<U>(static_cast<unsigned char>(byte))<<(i*8u);}return static_cast<T>(raw);
}
std::uint32_t count(std::size_t value,const char* name){if(value>kMaximumItems)throw IrError(std::string("FELBIN ")+name+" exceeds its limit");return static_cast<std::uint32_t>(value);}
void writeSpan(std::ostream& out,const IrSourceMapEntry::Span& span){writeLe<std::int32_t>(out,span.startLine);writeLe<std::int32_t>(out,span.startColumn);writeLe<std::int32_t>(out,span.endLine);writeLe<std::int32_t>(out,span.endColumn);}
IrSourceMapEntry::Span readSpan(std::istream& in){return{readLe<std::int32_t>(in),readLe<std::int32_t>(in),readLe<std::int32_t>(in),readLe<std::int32_t>(in)};}
void writeSymbols(std::ostream& out,const std::vector<IrSymbolRef>& symbols){writeLe<std::uint32_t>(out,count(symbols.size(),"symbol count"));for(const auto symbol:symbols){if(symbol==0)throw IrError("FELBIN symbol is invalid");writeLe<std::uint64_t>(out,symbol);}}
std::vector<IrSymbolRef> readSymbols(std::istream& in){const auto size=readLe<std::uint32_t>(in);if(size>kMaximumItems)throw IrError("FELBIN symbol count exceeds its limit");std::vector<IrSymbolRef> result;result.reserve(size);for(std::uint32_t i=0;i<size;++i){const auto symbol=readLe<std::uint64_t>(in);if(symbol==0)throw IrError("FELBIN symbol is invalid");result.push_back(symbol);}return result;}
void writeText(std::ostream& out,const std::string& text){writeLe<std::uint32_t>(out,count(text.size(),"text byte count"));out.write(text.data(),static_cast<std::streamsize>(text.size()));if(!out)throw IrError("cannot write FELBIN text");}
std::string readText(std::istream& in){const auto size=readLe<std::uint32_t>(in);if(size>kMaximumItems)throw IrError("FELBIN text exceeds its limit");std::string text(size,'\0');in.read(text.data(),static_cast<std::streamsize>(size));if(!in&&size!=0)throw IrError("FELBIN text is truncated");return text;}

void writeProgram(std::ostream& out,const IsaProgram& program){
    writeLe<std::uint16_t>(out,program.code.registerCount);writeLe<std::uint16_t>(out,0);
    writeLe<std::uint32_t>(out,count(program.code.words.size(),"code word count"));
    writeLe<std::uint32_t>(out,count(program.constants.size(),"constant count"));
    writeLe<std::uint32_t>(out,count(program.texts.size(),"text count"));
    writeLe<std::uint32_t>(out,count(program.symbols.size(),"symbol count"));
    writeLe<std::uint32_t>(out,count(program.sourceMap.size(),"source-map count"));
    for(const auto word:program.code.words)writeLe<std::uint32_t>(out,word);
    for(std::size_t i=0;i<program.constants.size();++i){const auto kind=program.constantKinds.empty()?IrConstantKind::Number:program.constantKinds.at(i);writeLe<std::uint8_t>(out,static_cast<std::uint8_t>(kind));writeLe<std::uint64_t>(out,program.constants[i]);}
    for(const auto& text:program.texts)writeText(out,text);
    for(const auto symbol:program.symbols){if(symbol==0)throw IrError("FELBIN symbol is invalid");writeLe<std::uint64_t>(out,symbol);}
    for(const auto& entry:program.sourceMap){writeLe<std::uint32_t>(out,count(entry.instructionWord,"source-map offset"));writeSpan(out,entry.sourceSpan);}
}

IsaProgram readProgram(std::istream& in,std::size_t procedureCount){
    IsaProgram program;program.code.registerCount=readLe<std::uint16_t>(in);if(readLe<std::uint16_t>(in)!=0)throw IrError("FELBIN program reserved field is nonzero");
    const auto codeCount=readLe<std::uint32_t>(in),constantCount=readLe<std::uint32_t>(in),textCount=readLe<std::uint32_t>(in),symbolCount=readLe<std::uint32_t>(in),sourceCount=readLe<std::uint32_t>(in);
    if(codeCount>kMaximumItems||constantCount>kMaximumItems||textCount>kMaximumItems||symbolCount>kMaximumItems||sourceCount>kMaximumItems)throw IrError("FELBIN program table exceeds its limit");
    program.code.words.reserve(codeCount);for(std::uint32_t i=0;i<codeCount;++i)program.code.words.push_back(readLe<std::uint32_t>(in));
    program.constants.reserve(constantCount);program.constantKinds.reserve(constantCount);for(std::uint32_t i=0;i<constantCount;++i){const auto raw=readLe<std::uint8_t>(in);if(raw>static_cast<std::uint8_t>(IrConstantKind::Text))throw IrError("FELBIN constant kind is invalid");program.constantKinds.push_back(static_cast<IrConstantKind>(raw));program.constants.push_back(readLe<std::uint64_t>(in));}
    program.texts.reserve(textCount);for(std::uint32_t i=0;i<textCount;++i)program.texts.push_back(readText(in));
    program.symbols.reserve(symbolCount);for(std::uint32_t i=0;i<symbolCount;++i){const auto symbol=readLe<std::uint64_t>(in);if(symbol==0)throw IrError("FELBIN symbol is invalid");program.symbols.push_back(symbol);}
    program.sourceMap.reserve(sourceCount);for(std::uint32_t i=0;i<sourceCount;++i)program.sourceMap.push_back({readLe<std::uint32_t>(in),readSpan(in)});
    for(std::size_t i=0;i<program.constants.size();++i)if(program.constantKinds[i]==IrConstantKind::Text&&program.constants[i]>=program.texts.size())throw IrError("FELBIN text constant index is invalid");
    IsaVerifier::verify(program.code,{program.constants.size(),program.symbols.size(),procedureCount});
    return program;
}

void writeProcedure(std::ostream& out,IrSymbolRef symbol,const IsaProcedure& procedure){writeLe<std::uint64_t>(out,symbol);writeSymbols(out,procedure.positionalParameters);writeSymbols(out,procedure.namedParameters);writeSpan(out,procedure.sourceSpan);writeProgram(out,procedure.program);}
IsaProcedure readProcedure(std::istream& in,IrSymbolRef& symbol,std::size_t procedureCount){symbol=readLe<std::uint64_t>(in);if(symbol==0)throw IrError("FELBIN procedure symbol is invalid");IsaProcedure result;result.positionalParameters=readSymbols(in);result.namedParameters=readSymbols(in);result.sourceSpan=readSpan(in);result.program=readProgram(in,procedureCount);return result;}

} // namespace

void writeBinaryIsa(const std::filesystem::path& path,const IsaModule& module){
    verifyIsaModule(module);const auto temporary=path.string()+".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)throw IrError("cannot create temporary FELBIN");out.write(kMagic.data(),kMagic.size());writeLe<std::uint32_t>(out,kFelidaeBinaryVersion);writeLe<std::uint32_t>(out,kEndian);writeLe<std::uint32_t>(out,module.isaVersion);writeLe<std::uint16_t>(out,module.entryProcedure);writeLe<std::uint16_t>(out,0);writeLe<std::uint32_t>(out,count(module.procedures.size(),"procedure count"));writeLe<std::uint32_t>(out,count(module.factTypes.size(),"fact-type count"));writeLe<std::uint32_t>(out,count(module.symbolNames.size(),"symbol-name count"));writeProgram(out,module.initializer);for(std::size_t i=0;i<module.procedures.size();++i)writeProcedure(out,module.procedureSymbols[i],module.procedures[i]);for(const auto& type:module.factTypes){writeLe<std::uint64_t>(out,type.symbol);writeSymbols(out,type.parents);writeSpan(out,type.sourceSpan);}for(const auto& entry:module.symbolNames){writeLe<std::uint64_t>(out,entry.symbol);writeText(out,entry.name);}out.close();if(!out)throw IrError("cannot write FELBIN");
#ifdef _WIN32
        if(!MoveFileExW(std::filesystem::path(temporary).wstring().c_str(),path.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw IrError("cannot replace FELBIN");
#else
        std::error_code error;std::filesystem::rename(temporary,path,error);if(error)throw IrError("cannot replace FELBIN: "+error.message());
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}

IsaModule loadBinaryIsa(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);if(!in)throw IrError("cannot open FELBIN: "+path.string());in.seekg(0,std::ios::end);const auto end=in.tellg();if(end<0||static_cast<std::uint64_t>(end)<kHeaderBytes)throw IrError("FELBIN is truncated");if(static_cast<std::uint64_t>(end)>kMaximumBytes)throw IrError("FELBIN exceeds its byte limit");in.seekg(0);std::array<char,8> magic{};in.read(magic.data(),magic.size());if(magic!=kMagic)throw IrError("FELBIN magic is invalid");if(readLe<std::uint32_t>(in)!=kFelidaeBinaryVersion)throw IrError("FELBIN version is unsupported");if(readLe<std::uint32_t>(in)!=kEndian)throw IrError("FELBIN endian marker is invalid");IsaModule module;module.isaVersion=readLe<std::uint32_t>(in);if(module.isaVersion!=kFelidaeIsaVersion)throw IrError("FELBIN ISA version is unsupported");module.entryProcedure=readLe<std::uint16_t>(in);if(readLe<std::uint16_t>(in)!=0)throw IrError("FELBIN header reserved field is nonzero");const auto procedureCount=readLe<std::uint32_t>(in),factCount=readLe<std::uint32_t>(in),symbolNameCount=readLe<std::uint32_t>(in);if(procedureCount==0||procedureCount>=kIsaShortIndexLimit||factCount>kMaximumItems||symbolNameCount>kMaximumItems)throw IrError("FELBIN module count is invalid");module.initializer=readProgram(in,procedureCount);module.procedures.reserve(procedureCount);module.procedureSymbols.reserve(procedureCount);for(std::uint32_t i=0;i<procedureCount;++i){IrSymbolRef symbol=0;module.procedures.push_back(readProcedure(in,symbol,procedureCount));module.procedureSymbols.push_back(symbol);}module.factTypes.reserve(factCount);for(std::uint32_t i=0;i<factCount;++i){IsaFactType type;type.symbol=readLe<std::uint64_t>(in);if(type.symbol==0)throw IrError("FELBIN fact type is invalid");type.parents=readSymbols(in);type.sourceSpan=readSpan(in);module.factTypes.push_back(std::move(type));}module.symbolNames.reserve(symbolNameCount);for(std::uint32_t i=0;i<symbolNameCount;++i)module.symbolNames.push_back({readLe<std::uint64_t>(in),readText(in)});if(in.peek()!=EOF)throw IrError("FELBIN has trailing data");verifyIsaModule(module);return module;
}

bool containsRuntimeSemanticOperation(const IsaModule& module){verifyIsaModule(module);const auto contains=[](const IsaProgram& program){for(std::size_t pc=0;pc<program.code.words.size();pc+=isaInstructionWidth(program.code.words,pc))if(decodeIsaWord(program.code.words[pc]).opcode==IsaOpcode::SemanticEval)return true;return false;};if(contains(module.initializer))return true;for(const auto& procedure:module.procedures)if(contains(procedure.program))return true;return false;}

VmDisplayContext makeIsaDisplayContext(const IsaModule& module){verifyIsaModule(module);auto names=std::make_shared<std::unordered_map<IrSymbolRef,std::string>>();names->reserve(module.symbolNames.size());for(const auto& entry:module.symbolNames)names->emplace(entry.symbol,entry.name);VmDisplayContext context;context.symbolDecoder=[names=std::move(names)](IrSymbolRef symbol){const auto found=names->find(symbol);return found==names->end()?std::string{}:found->second;};return context;}

} // namespace Felidae
