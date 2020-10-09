//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Interpreter.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/UserInterface/UserInterface.h"

#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/FrontendTool/Utils.h"

#include "llvm/Support/Signals.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ManagedStatic.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

// elix22 - Urho3D related 
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#if defined(WIN32) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

// If we are running with -verify a reported has to be returned as unsuccess.
// This is relevant especially for the test suite.
static int checkDiagErrors(clang::CompilerInstance* CI, unsigned* OutErrs = 0) {

  unsigned Errs = CI->getDiagnostics().getClient()->getNumErrors();

  if (CI->getDiagnosticOpts().VerifyDiagnostics) {
    // If there was an error that came from the verifier we must return 1 as
    // an exit code for the process. This will make the test fail as expected.
    clang::DiagnosticConsumer* Client = CI->getDiagnostics().getClient();
    Client->EndSourceFile();
    Errs = Client->getNumErrors();

    // The interpreter expects BeginSourceFile/EndSourceFiles to be balanced.
    Client->BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());
  }

  if (OutErrs)
    *OutErrs = Errs;

  return Errs ? EXIT_FAILURE : EXIT_SUCCESS;
}


// elix22 - Urho3D related start
bool isDir(std::string path) { 
    bool IsDir = false; 
    llvm::sys::fs::is_directory(path, IsDir);

    return IsDir;
}

  bool isFile(std::string path) {
  bool isFile = false;
  bool isSymFile = false;
  llvm::sys::fs::is_regular_file(path, isFile);
  llvm::sys::fs::is_symlink_file(path, isSymFile);

  return isFile || isSymFile;
}


void ReplaceString(std::string& subject, const std::string& search,
                   const std::string& replace) {
  size_t pos = 0;
  while ((pos = subject.find(search, pos)) != std::string::npos) {
    subject.replace(pos, search.length(), replace);
    pos += replace.length();
  }
}

void fixPath(std::string& path) {
  ReplaceString(path, "\\", "/");
  ReplaceString(path, "//", "/");
}


void getSourceCodeFilesInDirectory(const std::string& path,
                                   std::vector<std::string>& files) {

  std::error_code EC;
  for (llvm::sys::fs::directory_iterator File(path, EC), FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    bool IsDir = false;
    bool isFile = false;
    bool isSymFile = false;
    llvm::sys::fs::is_directory(File->path(), IsDir);
    llvm::sys::fs::is_regular_file(File->path(), isFile);
    llvm::sys::fs::is_symlink_file(File->path(), isSymFile);

    if (IsDir == true) {
      StringRef dirName = llvm::sys::path::filename(File->path());
      std::string childDirName = path + "/" + dirName.str();
      getSourceCodeFilesInDirectory(childDirName, files);
    } else if (isFile == true || isSymFile == true) {

      StringRef ext = llvm::sys::path::extension(File->path());
      StringRef FileName = llvm::sys::path::filename(File->path());
      if (ext == ".cpp" || ext == ".h" || ext == ".cc" || ext == ".hpp" ||
          ext == ".c") {
        std::string fullPath = path + "/" + FileName.str();
        files.push_back(fullPath);
      }
    }
  }
}

void getSourceCodeFilesInDirectory(
    cling::Interpreter& Interp, const std::string& path,
                                   std::vector<std::string>& files,
                                   std::vector<std::string>& headers) {

  std::error_code EC;
  for (llvm::sys::fs::directory_iterator File(path, EC), FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    bool IsDir = false;
    bool isFile = false;
    bool isSymFile = false;
    llvm::sys::fs::is_directory(File->path(), IsDir);
    llvm::sys::fs::is_regular_file(File->path(), isFile);
    llvm::sys::fs::is_symlink_file(File->path(), isSymFile);

    if (IsDir == true) {
      StringRef dirName = llvm::sys::path::filename(File->path());
      std::string childDirName = path + "/" + dirName.str();

      fixPath(childDirName);
      Interp.AddIncludePath(childDirName);
      getSourceCodeFilesInDirectory(Interp,childDirName, files, headers);

    } else if (isFile == true || isSymFile == true) {

      StringRef ext = llvm::sys::path::extension(File->path());
      StringRef FileName = llvm::sys::path::filename(File->path());
      if (ext == ".cpp"  || ext == ".cc" || ext == ".c") {
        std::string fullPath = path + "/" + FileName.str();

        fixPath(fullPath);
        files.push_back(fullPath);
      }
      else if ( ext == ".h" || ext == ".hpp" ) {
        std::string fullPath = path + "/" + FileName.str();
        fixPath(fullPath);
        headers.push_back(fullPath);
      }
    }
  }
}

bool addIncludePath(cling::Interpreter& Interp, std::string path) {
  bool res = false;
  if (isDir(path)) {
    Interp.AddIncludePath(path);
    res = true;
  } else {
    llvm::errs() << path << " does not exist \n";
  }
  return res;
}

bool loadFile(cling::Interpreter& Interp, std::string path) {
  bool res = false;
  if (isFile(path)) {
    Interp.loadFile(path);
    res = true;
  } else {
    llvm::errs() << path << " does not exist \n";
  }
  return res;
}

int Urho3DMain(cling::Interpreter& Interp) {

  cling::Interpreter::CompilationResult Result;
  cling::UserInterface Ui(Interp);
  const cling::InvocationOptions& Opts = Interp.getOptions();

  std::string cmd = "";
  std::string NL = "\n";

  std::string Urho3DHome  = Opts.Urho3DHome;

  if (Urho3DHome != "") {
    fixPath(Urho3DHome);

    if (!isDir(Urho3DHome)) {
      llvm::errs() << Urho3DHome << " does not exist \n";
      return EXIT_FAILURE;
    }

    if (!addIncludePath(Interp, Urho3DHome + "/include"))
      return EXIT_FAILURE;
    if (!addIncludePath(Interp, Urho3DHome + "/include/Urho3D"))
      return EXIT_FAILURE;
    if (!addIncludePath(Interp, Urho3DHome + "/include/Urho3D/ThirdParty"))
      return EXIT_FAILURE;
    if (!addIncludePath(Interp,
                        Urho3DHome + "/include/Urho3D/ThirdParty/Bullet"))
      return EXIT_FAILURE;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) || defined(__WIN32__)
    std::string urho3dLib = Urho3DHome + "/bin/Urho3D.dll";
    if (!loadFile(Interp, urho3dLib))
      return EXIT_FAILURE;
#elif defined(__APPLE__)
    std::string urho3dLib = Urho3DHome + "/lib/LibUrho3D.dylib";
    if (!loadFile(Interp, urho3dLib))return EXIT_FAILURE;
#else
    std::string urho3dLib = Urho3DHome + "/lib/libUrho3D.so";
    if (!loadFile(Interp, urho3dLib))return EXIT_FAILURE;
#endif

    }


  cmd += "#define URHO3D_CLING" + NL;
  cmd += "#define URHO3D_API" + NL;
  cmd += "#define URHO3D_ANGELSCRIPT" + NL;
  cmd += "#define URHO3D_LUA" + NL;
  cmd += "#define URHO3D_NAVIGATION" + NL;
  cmd += "#define URHO3D_NETWORK" + NL;
  cmd += "#define DURHO3D_URHO2D" + NL;
  cmd += "#define URHO3D_PHYSICS" + NL;
  cmd += "#define URHO3D_IK" + NL;
 
  for (const std::string& define : Opts.Defines) {
    cmd += "#define " + define + NL;
  } 

  cmd += "#include <Urho3DAll.h>" + NL;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) || defined(__WIN32__)
   cmd +=  "extern \"C\"  void __cdecl __std_reverse_trivially_swappable_8(void* "
      "_First, void* _Last) noexcept {}" + NL;
  cmd +=       "extern \"C\"  void __cdecl __std_reverse_trivially_swappable_4(void* "
      "_First, void* _Last) noexcept {}" + NL;
#endif

  for (const std::string& path : Opts.PathsToLoad) {

    if (llvm::sys::fs::is_directory(path) == false)
      continue;

    Interp.AddIncludePath(path);

    std::vector<std::string> headers;
    std::vector<std::string> files;
    getSourceCodeFilesInDirectory(Interp,path, files, headers);

    /*
    for (const std::string& header : headers) {
      cmd += "#include \"" + header + "\"" + NL;
    }
    */
    
    
    for (const std::string& file : files) {
        cmd += "#include \"" + file + "\"" + NL;
    }
  }


  std::string urho3dResourceDir = "";

  if (Opts.Urho3DResourcePrefixPath != "") {
    urho3dResourceDir = Opts.Urho3DResourcePrefixPath;
  } else if (Urho3DHome != "") {
    urho3dResourceDir = Urho3DHome + "/bin";
  }

  fixPath(urho3dResourceDir);

  std::string classDeclaration = "class Urho3DClingProxyApplication : public " +Opts.ApplicationClassName + NL;
  classDeclaration +=            "{" + NL;
  classDeclaration +=               "URHO3D_OBJECT(Urho3DClingProxyApplication,  " +Opts.ApplicationClassName + ");" + NL;
  classDeclaration +=               "Urho3DClingProxyApplication(Context* context):" +Opts.ApplicationClassName + "(context)" + NL;
  classDeclaration +=               "{" + NL;
  classDeclaration +=                    "engineParameters_[EP_RESOURCE_PREFIX_PATHS] = \"" +urho3dResourceDir + "\";" + NL;
  classDeclaration +=               "}" + NL;
  classDeclaration +=            "};" + NL;

  cmd += classDeclaration + NL;

  //printf("%s", cmd.c_str());
  
  Ui.getMetaProcessor()->process(cmd, Result, 0);
  if (Result == cling::Interpreter::CompilationResult::kFailure)
    return EXIT_FAILURE;


  Ui.getMetaProcessor()->process("Urho3D::SharedPtr<Urho3D::Context> context(new Urho3D::Context());", Result, 0);
  if (Result == cling::Interpreter::CompilationResult::kFailure)
    return EXIT_FAILURE;

  Ui.getMetaProcessor()->process("Urho3D::SharedPtr<Urho3DClingProxyApplication> application(new Urho3DClingProxyApplication(context));", Result, 0);
  if (Result == cling::Interpreter::CompilationResult::kFailure)
    return EXIT_FAILURE;

  Ui.getMetaProcessor()->process("application->Run();", Result, 0);
  if (Result == cling::Interpreter::CompilationResult::kFailure)
    return EXIT_FAILURE;

  return 0;
}
// elix22 - Urho3D related end

int main( int argc, char **argv ) {

  llvm::llvm_shutdown_obj shutdownTrigger;

  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram X(argc, argv);

#if defined(_WIN32) && defined(_MSC_VER)
  // Suppress error dialogs to avoid hangs on build nodes.
  // One can use an environment variable (Cling_GuiOnAssert) to enable
  // the error dialogs.
  const char *EnablePopups = getenv("Cling_GuiOnAssert");
  if (EnablePopups == nullptr || EnablePopups[0] == '0') {
    ::_set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  }
#endif

  // Set up the interpreter
  cling::Interpreter Interp(argc, argv);
  const cling::InvocationOptions& Opts = Interp.getOptions();

  if (!Interp.isValid()) {
    if (Opts.Help || Opts.ShowVersion)
      return EXIT_SUCCESS;

    unsigned ErrsReported = 0;
    if (clang::CompilerInstance* CI = Interp.getCIOrNull()) {
      // If output requested and execution succeeded let the DiagnosticsEngine
      // determine the result code
      if (Opts.CompilerOpts.HasOutput && ExecuteCompilerInvocation(CI))
        return checkDiagErrors(CI);

      checkDiagErrors(CI, &ErrsReported);
    }

    // If no errors have been reported, try perror
    if (ErrsReported == 0)
      ::perror("Could not create Interpreter instance");

    return EXIT_FAILURE;
  }

  Interp.AddIncludePath(".");

  for (const std::string& Lib : Opts.LibsToLoad)
    Interp.loadFile(Lib);

  // elix22 - Urho3D related
  if (Opts.ApplicationClassName != "") {
    return Urho3DMain(Interp);
  }

  cling::UserInterface Ui(Interp);
  // If we are not interactive we're supposed to parse files
  if (!Opts.IsInteractive()) {
    for (const std::string &Input : Opts.Inputs) {
      std::string Cmd;
      cling::Interpreter::CompilationResult Result;
      const std::string Filepath = Interp.lookupFileOrLibrary(Input);
      if (!Filepath.empty()) {
        std::ifstream File(Filepath);
        std::string Line;
        std::getline(File, Line);
        if (Line[0] == '#' && Line[1] == '!') {
          // TODO: Check whether the filename specified after #! is the current
          // executable.
          while (std::getline(File, Line)) {
            Ui.getMetaProcessor()->process(Line, Result, 0);
          }
          continue;
        }
        Cmd += ".x ";
      }
      Cmd += Input;
      Ui.getMetaProcessor()->process(Cmd, Result, 0);
    }
  }
  else {
    Ui.runInteractively(Opts.NoLogo);
  }

  // Only for test/OutputRedirect.C, but shouldn't affect performance too much.
  ::fflush(stdout);
  ::fflush(stderr);

  return checkDiagErrors(Interp.getCI());
}
