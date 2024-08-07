// Copyright 2019-2023 hdoc
// SPDX-License-Identifier: AGPL-3.0-only

#include "support/ParallelExecutor.hpp"
#include "spdlog/spdlog.h"

#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/VirtualFileSystem.h"

#include <atomic>

class MyDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  MyDiagnosticConsumer(std::string fileName) : fileName(fileName){};

  void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel, const clang::Diagnostic& Info) override {
    // Note: We deliberately don't call the parent implementation, which counts the number of warnings and errors
    // This is because those counts are used by the tool to determine whether a file was successfully processed
    // clang::DiagnosticConsumer::HandleDiagnostic(DiagLevel, Info);

    if (DiagLevel == clang::DiagnosticsEngine::Level::Error) {
      clang::SmallString<100> message;
      Info.FormatDiagnostic(message);
      // There's probably a better way of doing this...
      if (message.find("no such file or directory") != std::string::npos) {
        // The message already contains the file name; no need to repeat it here
        spdlog::error("{}", message.c_str());
      } else {
        // Running with --verbose sets the log level to info , so we emit additional errors as info
        // Message is very bare bones w/o any location information, but it's better than nothing
        spdlog::info("Encountered error while processing {}: {}", fileName, message.c_str());
      }
      numErrors++;
    } else if (DiagLevel == clang::DiagnosticsEngine::Level::Warning) {
      numWarnings++;
    }
  }

  // We have to keep count manually because we don't call the parent implementation
  size_t numWarnings = 0;
  size_t numErrors   = 0;

private:
  std::string fileName;
};

void hdoc::indexer::ParallelExecutor::execute(std::unique_ptr<clang::tooling::FrontendActionFactory> action) {
  std::atomic_size_t i             = 0;
  std::string        totalNumFiles = std::to_string(this->cmpdb.getAllFiles().size());

  std::vector<std::string> allFilesInCmpdb = this->cmpdb.getAllFiles();

  if (this->debugLimitNumIndexedFiles > 0) {
    allFilesInCmpdb.resize(this->debugLimitNumIndexedFiles);
    totalNumFiles = std::to_string(this->debugLimitNumIndexedFiles);
  }

  std::atomic_size_t totalWarnings = 0;
  std::atomic_size_t totalErrors   = 0;
  for (const std::string& file : allFilesInCmpdb) {
    this->pool.async(
        [&](const std::string path) {
          spdlog::info("[{}/{}] processing {}", ++i, totalNumFiles, path);

          // Each thread gets an independent copy of a VFS to allow different concurrent working directories
          llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = llvm::vfs::createPhysicalFileSystem().release();
          clang::tooling::ClangTool Tool(this->cmpdb, {path}, std::make_shared<clang::PCHContainerOperations>(), FS);

          // Append argument adjusters so that system includes and others are picked up on
          // TODO: determine if the -fsyntax-only flag actually does anything
          Tool.appendArgumentsAdjuster(clang::tooling::getClangStripOutputAdjuster());
          Tool.appendArgumentsAdjuster(clang::tooling::getClangStripDependencyFileAdjuster());
          Tool.appendArgumentsAdjuster(clang::tooling::getClangSyntaxOnlyAdjuster());
          Tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
              this->includePaths, clang::tooling::ArgumentInsertPosition::END));

          // We ignore most diagnostics by default, except for files that cannot be found.
          // Additional error messages are printed when using --verbose output.
          MyDiagnosticConsumer diagConsumer(path);
          Tool.setDiagnosticConsumer(&diagConsumer);

          // Disable error messages from the tool itself, as they don't add any value ("Error while processing <file>")
          Tool.setPrintErrorMessage(false);

          // Run the tool and print an error message if something goes wrong
          if (Tool.run(action.get())) {
            spdlog::error(
                "Clang failed to parse source file: {}. Information from this file may be missing from hdoc's output",
                path);
          }

          totalWarnings += diagConsumer.numWarnings;
          totalErrors += diagConsumer.numErrors;
        },
        file);
  }
  // Make sure all tasks have finished before resetting the working directory
  this->pool.wait();

  if (totalErrors > 0) {
    const auto logVerboseMsg =
        spdlog::get_level() > spdlog::level::info ? " (run with --verbose for more details)" : "";
    spdlog::error("Clang encountered {} errors and {} warnings{}", totalErrors, totalWarnings, logVerboseMsg);
  } else if (totalWarnings > 0) {
    spdlog::warn("Clang encountered {} warnings", totalWarnings);
  }
}
