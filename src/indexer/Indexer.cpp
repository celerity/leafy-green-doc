// Copyright 2019-2023 hdoc
// SPDX-License-Identifier: AGPL-3.0-only

#include <filesystem>

#include "spdlog/spdlog.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include "indexer/Indexer.hpp"
#include "indexer/Matchers.hpp"
#include "support/ParallelExecutor.hpp"
#include "support/StringUtils.hpp"

// Check if a symbol is a child of the given namespace
static bool isChild(const hdoc::types::Symbol& ns, const hdoc::types::Symbol& s) {
  return s.parentNamespaceID.raw() == ns.ID.raw();
}

void hdoc::indexer::Indexer::run() {
  spdlog::info("Starting indexing...");

  std::string err;
  const auto  stx = clang::tooling::JSONCommandLineSyntax::AutoDetect;
  const auto  cmpdb =
      clang::tooling::JSONCompilationDatabase::loadFromFile(this->cfg->compileCommandsJSON.string(), err, stx);

  if (cmpdb == nullptr) {
    spdlog::error("Unable to initialize compilation database ({})", err);
    return;
  }

  hdoc::indexer::matchers::FunctionMatcher  FunctionFinder(&this->index, this->cfg);
  hdoc::indexer::matchers::RecordMatcher    RecordFinder(&this->index, this->cfg);
  hdoc::indexer::matchers::EnumMatcher      EnumFinder(&this->index, this->cfg);
  hdoc::indexer::matchers::NamespaceMatcher NamespaceFinder(&this->index, this->cfg);
  hdoc::indexer::matchers::UsingMatcher     UsingFinder(&this->index, this->cfg);
  clang::ast_matchers::MatchFinder          Finder;
  Finder.addMatcher(FunctionFinder.getMatcher(), &FunctionFinder);
  Finder.addMatcher(RecordFinder.getMatcher(), &RecordFinder);
  Finder.addMatcher(EnumFinder.getMatcher(), &EnumFinder);
  Finder.addMatcher(NamespaceFinder.getMatcher(), &NamespaceFinder);
  Finder.addMatcher(UsingFinder.getMatcher(), &UsingFinder);

  // Add include search paths to clang invocation
  std::vector<std::string> includePaths = {};
  for (const std::string& d : cfg->includePaths) {
    // Ignore include paths that don't exist
    if (!std::filesystem::exists(d)) {
      spdlog::warn("Include path {} does not exist. Proceeding without it.", d);
      continue;
    }
    spdlog::info("Appending {} to list of include paths.", d);
    includePaths.emplace_back("-isystem" + d);
  }

  hdoc::indexer::ParallelExecutor tool(*cmpdb, includePaths, this->pool, this->cfg->debugLimitNumIndexedFiles);
  tool.execute(clang::tooling::newFrontendActionFactory(&Finder));
}

void hdoc::indexer::Indexer::resolveNamespaces() {
  spdlog::info("Indexer resolving namespaces.");
  for (auto& [k, ns] : this->index.namespaces.entries) {
    // Add all the direct children of this namespace to its children vector
    for (const auto& [k, v] : this->index.records.entries) {
      if (isChild(ns, v)) {
        ns.records.emplace_back(v.ID);
      }
    }
    for (const auto& [k, v] : this->index.enums.entries) {
      if (isChild(ns, v)) {
        ns.enums.emplace_back(v.ID);
      }
    }
    for (const auto& [k, v] : this->index.namespaces.entries) {
      if (isChild(ns, v)) {
        ns.namespaces.emplace_back(v.ID);
      }
    }
    for (const auto& [k, v] : this->index.aliases.entries) {
      if (isChild(ns, v)) {
        ns.usings.emplace_back(v.ID);
      }
    }
    for (const auto& [k, v] : this->index.functions.entries) {
      if (isChild(ns, v)) {
        ns.functions.emplace_back(v.ID);
      }
    }
  }
  spdlog::info("Indexer namespace resolution complete.");
}

void hdoc::indexer::Indexer::updateRecordNames() {
  spdlog::info("Indexer updating record names with inheritance information.");
  for (auto& [k, c] : this->index.records.entries) {
    if (c.baseRecords.size() > 0) {
      uint64_t count = 0;
      c.proto += " : ";
      for (const auto& baseRecord : c.baseRecords) {
        if (count > 0) {
          c.proto += ", ";
        }

        // Print the access type that indicates which kind of inheritance was used
        switch (baseRecord.access) {
        case clang::AS_public:
          c.proto += "public ";
          break;
        case clang::AS_private:
          c.proto += "private ";
          break;
        case clang::AS_protected:
          c.proto += "protected ";
          break;
        case clang::AS_none:
        // intentional fallthrough
        default:
          break;
        }

        c.proto += baseRecord.name;
        count++;
      }
    }
  }
}

void hdoc::indexer::Indexer::updateMemberFunctions() {
  for (auto& [k, c] : this->index.records.entries) {
    for (auto& symbol : c.methodIDs) {
      if (!this->index.functions.contains(symbol)) continue;

      auto& f = this->index.functions.entries.at(symbol);
      // split the proto into parts
      std::string templatePart = f.proto.substr(0, f.postTemplate);
      std::string preNamePart = f.proto.substr(f.postTemplate, f.nameStart - f.postTemplate);
      std::string restPart = f.proto.substr(f.nameStart);
      std::string name = f.name;
      // and update them individually
      auto fixTypeParam = [&](std::string& s) {
        for(size_t i=0; i<c.templateParams.size(); i++) {
          s = hdoc::utils::replaceAll(s, "type-parameter-0-" + std::to_string(i), c.templateParams[i].name);
        }
      };
      fixTypeParam(templatePart);
      fixTypeParam(preNamePart);
      fixTypeParam(restPart);
      fixTypeParam(name);
      // so that we can reconstruct the offsets
      std::string newProto = templatePart + preNamePart + restPart;
      if(newProto != f.proto) {
        spdlog::debug("Updating function proto from\n  {} to \n  {}\n  name: {} -> {}", f.proto, newProto, f.name, name);
        f.proto = templatePart + preNamePart + restPart;
        f.name = name;
        f.postTemplate = templatePart.size();
        f.nameStart = templatePart.size() + preNamePart.size();
      }
      // also fix parameters
      for(auto& param : f.params) {
        fixTypeParam(param.type.name);
        fixTypeParam(param.defaultValue);
      }
    }
  }
}

void hdoc::indexer::Indexer::printStats() const {

  const auto printDatabaseSize = []<typename T>(const char* name, const types::Database<T>& db) {
    spdlog::info("{:12}: {:8} matches, {:6} indexed, {:6} KiB total size",
                 name,
                 db.numMatches,
                 db.entries.size(),
                 db.entries.size() * sizeof(T) / 1024);
  };

  printDatabaseSize("Functions", this->index.functions);
  printDatabaseSize("Records", this->index.records);
  printDatabaseSize("Enums", this->index.enums);
  printDatabaseSize("Namespaces", this->index.namespaces);
  printDatabaseSize("Usings", this->index.aliases);
}

void hdoc::indexer::Indexer::pruneMethods() {
  // If a method's parent isn't in the index, it was filtered out and not indexed.
  // ergo, it's children shouldn't be indexed either, so we remove them
  std::vector<hdoc::types::SymbolID> toBePruned;
  for (const auto& [k, v] : this->index.functions.entries) {
    if (v.isRecordMember && !this->index.records.contains(v.parentNamespaceID)) {
      toBePruned.emplace_back(k);
    }
  }
  // Remove methods from index
  for (const auto& deadSymbolID : toBePruned) {
    this->index.functions.entries.erase(deadSymbolID);
  }
  spdlog::info("Pruned {} functions from the database.", toBePruned.size());
}

void hdoc::indexer::Indexer::pruneTypeRefs() {
  auto haveId = [&](const hdoc::types::SymbolID& id) {
    return this->index.records.contains(id) || this->index.enums.contains(id) || this->index.aliases.contains(id);
  };

  for (auto& [k, v] : this->index.functions.entries) {
    if (haveId(v.returnType.id) == false) {
      v.returnType.id = hdoc::types::SymbolID();
    }

    for (auto& param : v.params) {
      if (haveId(param.type.id) == false) {
        param.type.id = hdoc::types::SymbolID();
      }
    }
  }

  for (auto& [k, v] : this->index.records.entries) {
    for (auto& var : v.vars) {
      if (haveId(var.type.id) == false) {
        var.type.id = hdoc::types::SymbolID();
      }
    }
  }

  for (auto& [k, v] : this->index.aliases.entries) {
    if (haveId(v.target.id) == false) {
      v.target.id = hdoc::types::SymbolID();
    }
  }
}

const hdoc::types::Index* hdoc::indexer::Indexer::dump() const {
  return &this->index;
}
