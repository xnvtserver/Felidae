#include "form/BinaryIsa.h"
#include "form/FelidaeIsa.h"
#include "form/IsaLowerer.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>

namespace {
bool rejects(const std::function<void()>& action){try{action();}catch(const Felidae::IrError&){return true;}return false;}
}

int main(){
    using namespace Felidae;
    const std::filesystem::path testOutputDirectory(FELIDAE_TEST_OUTPUT_DIR);
    std::filesystem::create_directories(testOutputDirectory);
    constexpr IrSymbolRef kMain=0x1001;
    IrModule ir;ir.entryProcedure=kMain;ir.ir.registerCount=1;ir.ir.symbols={kMain};ir.ir.words={static_cast<IrWord>(IrOpcode::Call),0,0,0,static_cast<IrWord>(IrOpcode::Return),0,0,static_cast<IrWord>(IrOpcode::End)};
    IrProcedure procedure;procedure.ir.registerCount=1;procedure.ir.constants={encodeIrNumber(42.0)};procedure.ir.constantKinds={IrConstantKind::Number};procedure.ir.words={static_cast<IrWord>(IrOpcode::LoadConst),0,0,static_cast<IrWord>(IrOpcode::Return),0,0,static_cast<IrWord>(IrOpcode::End)};ir.procedures.emplace(kMain,std::move(procedure));
    const auto module=IsaLowerer::lowerModule(ir);verifyIsaModule(module);
    const auto path=testOutputDirectory/"felidae_isa_binary_test.bin";writeBinaryIsa(path,module);
    std::array<char,8> magic{};std::uint32_t binaryVersion=0,isaVersion=0;{std::ifstream input(path,std::ios::binary);input.read(magic.data(),8);input.read(reinterpret_cast<char*>(&binaryVersion),4);input.seekg(16);input.read(reinterpret_cast<char*>(&isaVersion),4);}
    assert((magic==std::array<char,8>{'F','E','L','B','I','N','\0','\0'}));assert(binaryVersion==kFelidaeBinaryVersion);assert(isaVersion==kFelidaeIsaVersion);
    const auto loaded=loadBinaryIsa(path);assert(loaded.symbolNames.empty());FelidaeKnowledgeRuntime runtime;RegisterVm vm;assert(std::get<double>(vm.executeIsaMain(loaded,runtime))==42.0);
    auto unknown=module;unknown.initializer.code.words[0]=0xfeu;assert(rejects([&]{writeBinaryIsa(path,unknown);}));
    auto invalidRegister=module;invalidRegister.initializer.code.words[0]=encodeIsaABC(IsaOpcode::Call,255,0);assert(rejects([&]{writeBinaryIsa(path,invalidRegister);}));
    auto invalidProcedure=module;invalidProcedure.initializer.code.words[1]=99;assert(rejects([&]{writeBinaryIsa(path,invalidProcedure);}));
    const auto corrupt=[&](const char* name,std::streamoff offset,std::array<char,4> bytes){const auto candidate=testOutputDirectory/name;std::filesystem::copy_file(path,candidate,std::filesystem::copy_options::overwrite_existing);{std::fstream file(candidate,std::ios::binary|std::ios::in|std::ios::out);file.seekp(offset);file.write(bytes.data(),4);}assert(rejects([&]{(void)loadBinaryIsa(candidate);}));std::error_code ignored;std::filesystem::remove(candidate,ignored);};
    corrupt("felidae_bad_container.bin",8,{0,0,0,0});corrupt("felidae_bad_isa.bin",16,{static_cast<char>(0xff),static_cast<char>(0xff),static_cast<char>(0xff),static_cast<char>(0xff)});
    const auto truncated=testOutputDirectory/"felidae_truncated.bin";{std::ofstream output(truncated,std::ios::binary);output.write("FELBIN",6);}assert(rejects([&]{(void)loadBinaryIsa(truncated);}));
    std::error_code ignored;std::filesystem::remove(path,ignored);std::filesystem::remove(truncated,ignored);return 0;
}
