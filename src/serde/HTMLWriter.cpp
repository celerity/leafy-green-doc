// Copyright 2019-2023 hdoc
// SPDX-License-Identifier: AGPL-3.0-only

#include "spdlog/spdlog.h"
#include "spdlog/fmt/fmt.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Format/Format.h"
#include "llvm/Support/JSON.h"

#include <filesystem>
#include <fstream>
#include <stack>
#include <string>

#include "serde/CppReferenceURLs.hpp"
#include "serde/HTMLWriter.hpp"
#include "serde/SerdeUtils.hpp"
#include "support/MarkdownConverter.hpp"
#include "support/StringUtils.hpp"
#include "types/Symbols.hpp"

/// Implementation of to_string() for Clang member variable access specifier
static std::string to_string(const clang::AccessSpecifier& access) {
  switch (access) {
  case clang::AccessSpecifier::AS_public:
    return "public";
  case clang::AccessSpecifier::AS_protected:
    return "protected";
  case clang::AccessSpecifier::AS_private:
    return "private";
  case clang::AccessSpecifier::AS_none:
    return "none";
  default:
    return "unknown";
  }
}

extern uint8_t      ___assets_styles_css[];
extern uint8_t      ___assets_favicon_ico[];
extern uint8_t      ___assets_favicon_32x32_png[];
extern uint8_t      ___assets_favicon_16x16_png[];
extern uint8_t      ___assets_apple_touch_icon_png[];
extern uint8_t      ___assets_search_js[];
extern uint8_t      ___assets_worker_js[];
extern uint8_t      ___assets_katex_min_css[];
extern uint8_t      ___assets_katex_min_js[];
extern uint8_t      ___assets_auto_render_min_js[];
extern uint8_t      ___assets_highlight_min_js[];
extern uint8_t      ___assets_index_min_js[];
extern unsigned int ___assets_styles_css_len;
extern unsigned int ___assets_favicon_ico_len;
extern unsigned int ___assets_favicon_32x32_png_len;
extern unsigned int ___assets_favicon_16x16_png_len;
extern unsigned int ___assets_apple_touch_icon_png_len;
extern unsigned int ___assets_search_js_len;
extern unsigned int ___assets_worker_js_len;
extern unsigned int ___assets_katex_min_css_len;
extern unsigned int ___assets_katex_min_js_len;
extern unsigned int ___assets_auto_render_min_js_len;
extern unsigned int ___assets_highlight_min_js_len;
extern unsigned int ___assets_index_min_js_len;

hdoc::serde::HTMLWriter::HTMLWriter(const hdoc::types::Index*  index,
                                    const hdoc::types::Config* cfg,
                                    llvm::ThreadPool&          pool)
    : index(index), cfg(cfg), pool(pool) {
  // Create the directory where the HTML files will be placed
  std::error_code ec;
  if (std::filesystem::exists(this->cfg->outputDir) == false) {
    if (std::filesystem::create_directories(this->cfg->outputDir, ec) == false) {
      spdlog::error("Creation of directory {} failed with the following error message: '{}'. Exiting.",
                    this->cfg->outputDir.string(),
                    ec.message());
      std::exit(1);
    }
  }

  // hdoc bundles assets (favicons, CSS) with the executable to simplify deployment.
  // The following code collects the files (converted to char arrays in the build process)
  // and outputs them. The process looks janky but it's simple and it works.
  struct BundledFile {
    const unsigned int          len;
    const uint8_t*              file;
    const std::filesystem::path path;
  };

  std::vector<BundledFile> bundledFiles = {
      {___assets_apple_touch_icon_png_len, ___assets_apple_touch_icon_png, cfg->outputDir / "apple-touch-icon.png"},
      {___assets_favicon_16x16_png_len, ___assets_favicon_16x16_png, cfg->outputDir / "favicon-16x16.png"},
      {___assets_favicon_32x32_png_len, ___assets_favicon_32x32_png, cfg->outputDir / "favicon-32x32.png"},
      {___assets_favicon_ico_len, ___assets_favicon_ico, cfg->outputDir / "favicon.ico"},
      {___assets_styles_css_len, ___assets_styles_css, cfg->outputDir / "styles.css"},
      {___assets_search_js_len, ___assets_search_js, cfg->outputDir / "search.js"},
      {___assets_worker_js_len, ___assets_worker_js, cfg->outputDir / "worker.js"},
      {___assets_katex_min_css_len, ___assets_katex_min_css, cfg->outputDir / "katex.min.css"},
      {___assets_katex_min_js_len, ___assets_katex_min_js, cfg->outputDir / "katex.min.js"},
      {___assets_auto_render_min_js_len, ___assets_auto_render_min_js, cfg->outputDir / "auto-render.min.js"},
      {___assets_highlight_min_js_len, ___assets_highlight_min_js, cfg->outputDir / "highlight.min.js"},
      {___assets_index_min_js_len, ___assets_index_min_js, cfg->outputDir / "index.min.js"},
  };

  for (const auto& file : bundledFiles) {
    std::ofstream out(file.path, std::ios::binary);
    out.write((char*)file.file, file.len);
    out.close();
  }
}

std::string escapeForHTML(const std::string& in) {
  std::string str = in;
  str = hdoc::utils::replaceAll(str, "&", "&amp;");
  str = hdoc::utils::replaceAll(str, "<", "&lt;");
  str = hdoc::utils::replaceAll(str, ">", "&gt;");
  str = hdoc::utils::replaceAll(str, "\"", "&quot;");
  str = hdoc::utils::replaceAll(str, "'", "&apos;");
  return str;
}

template<typename SymbolType>
std::string entryPageUrl(bool topLevel) {
  const std::string suffix = "/index.html";
  const std::string prefix = topLevel ? "" : "../";
  return prefix + SymbolType().directory() + suffix;
}

void appendEntryPageLinks(CTML::Node& node, bool topLevel) {
  node.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Namespaces").SetAttr("href", entryPageUrl<hdoc::types::NamespaceSymbol>(topLevel))));
  node.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Records").SetAttr("href", entryPageUrl<hdoc::types::RecordSymbol>(topLevel))));
  node.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Enums").SetAttr("href", entryPageUrl<hdoc::types::EnumSymbol>(topLevel))));
  node.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Functions").SetAttr("href", entryPageUrl<hdoc::types::FunctionSymbol>(topLevel))));
  node.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Aliases").SetAttr("href", entryPageUrl<hdoc::types::AliasSymbol>(topLevel))));
}

/// Create a new HTML page with standard structure
/// Optional sidebar, CSS styling, favicons, footer, etc.
static void printNewPage(const hdoc::types::Config&   cfg,
                         CTML::Node                   main,
                         const std::filesystem::path& path,
                         const std::string_view       pageTitle,
                         CTML::Node                   breadcrumbs = CTML::Node(),
                         bool topLevel = false) {
  CTML::Document html;

  // create path directories if they don't exist
  std::filesystem::create_directories(path.parent_path());

  if(!cfg.minimalOutput) {

    std::string dirPrefix = topLevel ? "" : "../";

    // Create the header, which includes Bulma CSS framework
    html.AppendNodeToHead(CTML::Node("meta").SetAttr("charset", "utf-8"));
    html.AppendNodeToHead(
        CTML::Node("meta").SetAttr("name", "viewport").SetAttr("content", "width=device-width, initial-scale=1"));
    html.AppendNodeToHead(CTML::Node("title", std::string(pageTitle)));

    // Use our custom css which is a modified version of bulma
    html.AppendNodeToHead(CTML::Node("link").SetAttr("rel", "stylesheet").SetAttr("href", dirPrefix + "styles.css"));

    // highlight.js scripts
    html.AppendNodeToHead(CTML::Node("script").SetAttr("src", dirPrefix + "highlight.min.js"));
    html.AppendNodeToHead(CTML::Node("script", "hljs.highlightAll();"));

    // KaTeX configuration
    html.AppendNodeToHead(CTML::Node("link").SetAttr("rel", "stylesheet").SetAttr("href", dirPrefix + "katex.min.css"));
    html.AppendNodeToHead(CTML::Node("script").SetAttr("src", dirPrefix + "katex.min.js"));
    html.AppendNodeToHead(CTML::Node("script").SetAttr("src", dirPrefix + "auto-render.min.js"));
    const char* katexConfiguration = R"(
      document.addEventListener("DOMContentLoaded", function() {
        renderMathInElement(document.body, {
          delimiters: [
            {left: '$$', right: '$$', display: true},
            {left: '$', right: '$', display: false},
          ],
        });
      });
    )";
    html.AppendNodeToHead(CTML::Node("script").AppendRawHTML(katexConfiguration));

    // Favicons
    html.AppendNodeToHead(CTML::Node("link")
                              .SetAttr("rel", "apple-touch-icon")
                              .SetAttr("sizes", "180x180")
                              .SetAttr("href", dirPrefix + "apple-touch-icon.png"));
    html.AppendNodeToHead(CTML::Node("link")
                              .SetAttr("rel", "icon")
                              .SetAttr("type", "image/png")
                              .SetAttr("sizes", "32x32")
                              .SetAttr("href", dirPrefix + "favicon-32x32.png"));
    html.AppendNodeToHead(CTML::Node("link")
                              .SetAttr("rel", "icon")
                              .SetAttr("type", "image/png")
                              .SetAttr("sizes", "16x16")
                              .SetAttr("href", dirPrefix + "favicon-16x16.png"));

    CTML::Node wrapperDiv   = CTML::Node("div#wrapper");
    CTML::Node section      = CTML::Node("section.section");
    CTML::Node containerDiv = CTML::Node("div.container");

    // Create a sidebar with navigation links etc
    auto columnsDiv = CTML::Node("div.columns");
    auto mainColumn = CTML::Node("div.column").SetAttr("style", "overflow-x: auto");
    auto aside      = CTML::Node("aside.column is-one-fifth");
    auto menuUL     = CTML::Node("ul.menu-list");

    menuUL.AddChild(CTML::Node("p.is-size-4", cfg.projectName + (cfg.projectVersion == "" ? "" : " " + cfg.projectVersion)));
    menuUL.AddChild(CTML::Node("p.menu-label", "Navigation"));
    menuUL.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Home").SetAttr("href", dirPrefix + "index.html")));
    menuUL.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Search").SetAttr("href", dirPrefix + "search.html")));
    if (cfg.gitRepoURL != "") {
      menuUL.AddChild(CTML::Node("li").AddChild(CTML::Node("a", "Repository").SetAttr("href", cfg.gitRepoURL)));
    }

    // Add paths to markdown pages converted to HTML, if any were provided
    if (cfg.mdPaths.size() > 0) {
      menuUL.AddChild(CTML::Node("p.menu-label", "Pages"));
      for (const auto& f : cfg.mdPaths) {
        std::string path = "doc" + f.filename().replace_extension("html").string();
        std::string name = f.filename().stem().string();
        menuUL.AddChild(CTML::Node("li").AddChild(CTML::Node("a", name).SetAttr("href", dirPrefix + path)));
      }
    }

    // Add links to all of the standard sections
    menuUL.AddChild(CTML::Node("p.menu-label", "API Documentation"));
    appendEntryPageLinks(menuUL, topLevel);
    aside.AddChild(menuUL);

    columnsDiv.AddChild(aside);
    columnsDiv.AddChild(mainColumn.AddChild(breadcrumbs).AddChild(main.SetAttr("class", "content")));
    containerDiv.AddChild(columnsDiv);
    section.AddChild(containerDiv);
    wrapperDiv.AddChild(section);
    html.AppendNodeToBody(wrapperDiv);

    // Create footer with creation date and details
    CTML::Node p1 = CTML::Node(
        "p", "Documentation for " + cfg.projectName + (cfg.projectVersion == "" ? "." : " " + cfg.projectVersion + "."));
    CTML::Node p2 = CTML::Node("p", "Generated by ")
                        .AddChild(CTML::Node("a").SetAttr("href", "https://github.com/PeterTh/hdoc").AppendRawHTML("&#129388;doc"))
                        .AppendText(" version " + cfg.hdocVersion + " on " + cfg.timestamp + ".");
    CTML::Node p3 = CTML::Node("p.has-text-grey-light", "19AD43E11B2996");
    html.AppendNodeToBody(CTML::Node("footer.footer").AddChild(p1).AddChild(p2).AddChild(p3));

    // Dump to a file
    std::ofstream(path) << html.ToString();
  } else {
    // prevent breadcrumbs from showing if they are empty
    // (i.e. on top level pages or unsupported contexts - provide info in the latter case so that can be fixed)
    std::string crumbsHTML = breadcrumbs.ToString();
    if(crumbsHTML == "<></>") {
      crumbsHTML = "";
      if(!topLevel && path.filename() != "index.html") {
        spdlog::warn("No breadcrumbs found for page '{}'", path.generic_string());
      }
    }
    std::ofstream(path) << crumbsHTML << std::endl << main.ToString();
  }
}

/// Return a short string describing a symbol for its entry in the overview list
/// If the string contains display math we automatically reject it since it will ruin the formatting
static std::string getSymbolBlurb(const hdoc::types::Symbol& s) {
  // TODO: this is not the most efficient way to write this...
  std::string ret = "";
  if (s.docComment != "") {
    if (s.docComment.size() > 64) {
      ret = " - " + s.docComment.substr(0, 63) + "...";
    } else {
      ret = " - " + s.docComment;
    }
  }

  if (s.briefComment != "") {
    // Despite the name, briefComments may be very long and may need
    // to be truncated to avoid taking up too much space in the HTML output.
    if (s.briefComment.size() > 64) {
      ret = " - " + s.briefComment.substr(0, 63) + "...";
    } else {
      ret = " - " + s.briefComment;
    }
  }

  if (ret.find("$$") != std::string::npos) {
    return "";
  } else {
    return ret;
  }
}

// for freestanding function groups, use the first you find
static std::string getSymbolBlurb(const hdoc::types::FreestandingFunction& f, const hdoc::types::Index& index) {
  for(auto f : f.functionIDs) {
    auto fs = index.functions.entries.at(f);
    std::string ret = getSymbolBlurb(fs);
    if(ret != "") {
      return ret;
    }
  }
  return "";
}

/// Run clang-format with a custom style over the given string
std::string hdoc::serde::clangFormat(const std::string_view s, const uint64_t& columnLimit) {
  // Run clang-format over function name to break width to 50 chars
  auto style              = clang::format::getChromiumStyle(clang::format::FormatStyle::LK_Cpp);
  style.ColumnLimit       = columnLimit;
  style.BreakBeforeBraces = clang::format::FormatStyle::BS_Attach;
  auto formattedName =
      clang::tooling::applyAllReplacements(s, clang::format::reformat(style, s, {clang::tooling::Range(0, s.size())}));

  return formattedName.get();
}

/// Returns the "bare" type name (i.e. type name with no qualifiers, pointers, or references)
/// for a given type name.
/// For example, and input of `const Type<int> **` becomes `Type`
std::string hdoc::serde::getBareTypeName(const std::string_view typeName) {
  std::string str = std::string(typeName);

  // Strip away type qualifiers
  hdoc::utils::replaceFirst(str, "const ", "");
  hdoc::utils::replaceFirst(str, "volatile ", "");
  hdoc::utils::replaceFirst(str, "restrict ", "");
  hdoc::utils::replaceFirst(str, "struct ", "");
  hdoc::utils::replaceFirst(str, "union ", "");

  str = str.substr(0, str.find("<"));
  str = str.substr(0, str.find("&"));
  str = str.substr(0, str.find("*"));
  str = str.substr(0, str.find("("));
  str = str.substr(0, str.find("["));
  hdoc::utils::rtrim(str);

  return str;
}

/// Replaces type names in a function proto with hyperlinked references to
/// those types. Works for indexed records and std:: types found in the map above.
std::string hdoc::serde::HTMLWriter::getHyperlinkedFunctionProto(const std::string_view             proto,
                                                                 const hdoc::types::FunctionSymbol& f) const {
  std::string str = std::string(proto);

  str = escapeForHTML(str);

  std::size_t index              = 0;
  std::string bareReturnTypeName = getBareTypeName(f.returnType.name);
  if (f.returnType.id.hashValue != 0) {
    std::string targetUrl = getURLForSymbol(f.returnType.id, true);
    if(targetUrl != "") {
      std::string replacement = "<a href=\"" + targetUrl + "\">" + bareReturnTypeName + "</a>";
      index                   = hdoc::utils::replaceFirst(str, bareReturnTypeName, replacement, index);
    }
  }

  if (bareReturnTypeName.substr(0, 5) == "std::") {
    if (StdTypeURLMap.find(bareReturnTypeName) != StdTypeURLMap.end()) {
      std::string replacement = "<a href=\"" + std::string(cppreferenceURL) + StdTypeURLMap.at(bareReturnTypeName) +
                                "\">" + bareReturnTypeName + "</a>";
      index = hdoc::utils::replaceFirst(str, bareReturnTypeName, replacement, index);
    }
  }

  for (const auto& param : f.params) {
    std::string bareParamTypeName = getBareTypeName(param.type.name);
    if (param.type.id.hashValue != 0) {
      std::string targetUrl = getURLForSymbol(param.type.id, true);
      if(targetUrl != "") {
        std::string replacement = "<a href=\"" + targetUrl + "\">" + bareParamTypeName + "</a>";
        index                   = hdoc::utils::replaceFirst(str, bareParamTypeName, replacement, index);
      }
    }

    if (bareParamTypeName.substr(0, 5) == "std::") {
      if (StdTypeURLMap.find(bareParamTypeName) != StdTypeURLMap.end()) {
        std::string replacement = "<a href=\"" + std::string(cppreferenceURL) + StdTypeURLMap.at(bareParamTypeName) +
                                  "\">" + bareParamTypeName + "</a>";
        index = hdoc::utils::replaceFirst(str, bareParamTypeName, replacement, index);
      }
    }
  }

  return str;
}

/// Returns the typename as raw HTML with hyperlinks where possible.
/// Indexed types are hyperlinked to, as are certain std:: types.
/// All others are returned without hyperlinks as the plain type name.
std::string hdoc::serde::HTMLWriter::getHyperlinkedTypeName(const hdoc::types::TypeRef& type) const {
  std::string fullTypeName = type.name;
  std::string bareTypeName = hdoc::serde::getBareTypeName(fullTypeName);

  fullTypeName = hdoc::serde::clangFormat(fullTypeName);
  fullTypeName = escapeForHTML(fullTypeName);

  if (type.id.raw() == 0) {
    // If it's a std:: type, then try to link to its cppreference page.
    if (bareTypeName.substr(0, 5) == "std::" && StdTypeURLMap.find(bareTypeName) != StdTypeURLMap.end()) {
      std::string replacement =
          "<a href=\"" + std::string(cppreferenceURL) + StdTypeURLMap.at(bareTypeName) + "\">" + bareTypeName + "</a>";
      hdoc::utils::replaceFirst(fullTypeName, bareTypeName, replacement);

      return fullTypeName;
    }
    // Otherwise, return it as-is.
    else {
      return fullTypeName;
    }
  }
  // The type is in the database, so we link to it.
  else {
    std::string replacement = "<a href=\"" + getURLForSymbol(type.id, true) + "\">" + bareTypeName + "</a>";
    hdoc::utils::replaceFirst(fullTypeName, bareTypeName, replacement);
    return fullTypeName;
  }
}

/// Returns an HTML node indicating where the s is declared.
/// A hyperlink to the exact line in the source file (for GitHub and GitLab) is returned
/// if gitRepoURL is provided.
static CTML::Node getDeclaredAtNode(const hdoc::types::Symbol& s,
                                    const std::string_view     gitRepoURL       = "",
                                    const std::string_view     gitDefaultBranch = "") {
  auto p = CTML::Node("p", "Declared at: ");
  if (gitRepoURL == "") {
    return p.AddChild(CTML::Node("span.is-family-code", s.file + ":" + std::to_string(s.line)));
  } else {
    return p.AddChild(CTML::Node("a.is-family-code", s.file + ":" + std::to_string(s.line))
                          .SetAttr("href",
                                   std::string(gitRepoURL) + "blob/" + std::string(gitDefaultBranch) + "/" + s.file +
                                       "#L" + std::to_string(s.line)));
  }
}

/// Creates a Bulma breadcrumb node to make the provenance of the current symbol more clear and aid in navigation.
CTML::Node hdoc::serde::HTMLWriter::getBreadcrumbNode(const std::string&         prefix,
                                                      const hdoc::types::Symbol& s,
                                                      const hdoc::types::Index&  index) const {
  // Symbols that have no parents don't have any breadcrumbs.
  if (s.parentNamespaceID.raw() == 0) {
    return CTML::Node();
  }

  auto nav = CTML::Node("nav.breadcrumb has-arrow-separator").SetAttr("aria-label", "breadcrumbs");
  auto ul  = CTML::Node("ul");

  struct ParentSymbol {
    std::string         symbolType;
    hdoc::types::Symbol symbol;
  };

  // Construct a LIFO stack of parents for the current symbol.
  // LIFO is used because we need to print the nodes into HTML in reverse order.
  std::stack<ParentSymbol> stack;
  hdoc::types::Symbol      parent = s;
  while (true) {
    if (index.namespaces.contains(parent.parentNamespaceID)) {
      const auto& newParent = index.namespaces.entries.at(parent.parentNamespaceID);
      stack.push({"namespace", newParent});
      parent = newParent;
    } else if (index.records.contains(parent.parentNamespaceID)) {
      const auto& newParent = index.records.entries.at(parent.parentNamespaceID);
      stack.push({newParent.type, newParent});
      parent = newParent;
    } else {
      break;
    }
  }

  // Create the HTML nodes for the parent symbols of the current node.
  while (!stack.empty()) {
    const auto parent = stack.top();
    stack.pop();

    auto li = CTML::Node("li");
    auto a  = CTML::Node();
    if (parent.symbolType == "namespace") {
      a = CTML::Node("a").SetAttr("href", entryPageUrl<hdoc::types::NamespaceSymbol>(false) + "#" + parent.symbol.ID.str());
    } else {
      a = CTML::Node("a").SetAttr("href", getURLForSymbol(parent.symbol.ID, true));
    }
    auto span = CTML::Node("span", parent.symbolType + " " + parent.symbol.name);

    ul.AddChild(li.AddChild(a.AddChild(span)));
  }

  // Add the final breadcrumb, which is the actual symbol itself.
  auto li   = CTML::Node("li.is-active");
  auto a    = CTML::Node("a").SetAttr("aria-current", "page" + s.ID.str());
  auto span = CTML::Node("span", prefix + " " + s.name);
  ul.AddChild(li.AddChild(a.AddChild(span)));

  return nav.AddChild(ul);
}

void appendAsMarkdown(const std::string comment, CTML::Node& node) {
  if (comment != "") {
    hdoc::utils::MarkdownConverter converter(comment);
    auto htmlstring = converter.getHTMLString();
    if(!htmlstring.empty()) {
      node.AddChild(CTML::Node("p").AppendRawHTML(htmlstring));
    } else {
      node.AddChild(CTML::Node("p", comment));
    }
  }
}

/// Print a function to main
void hdoc::serde::HTMLWriter::printFunction(const hdoc::types::FunctionSymbol& f,
                                            CTML::Node&                        main,
                                            const std::string_view             gitRepoURL,
                                            const std::string_view             gitDefaultBranch) const {
  // Print function return type, name, and parameters as section header
  std::string proto = getHyperlinkedFunctionProto(hdoc::serde::clangFormat(f.proto), f);
  auto        inner = CTML::Node("code.hdoc-function-code.language-cpp").AppendRawHTML(proto);
  main.AddChild(CTML::Node("h3#" + f.ID.str())
                    .AddChild(CTML::Node("pre.p-0.hdoc-pre-parent")
                                  .AddChild(CTML::Node("a.is-size-4", "¶")
                                                .SetAttr("class", "hdoc-permalink-icon")
                                                .SetAttr("href", "#" + f.ID.str()))
                                  .AddChild(inner)));

  // Print function description only if there's an associated comment
  if (f.briefComment != "" || f.docComment != "") {
    main.AddChild(CTML::Node("h4", "Description"));
  }
  appendAsMarkdown(f.briefComment, main);
  appendAsMarkdown(f.docComment, main);

  main.AddChild(getDeclaredAtNode(f, gitRepoURL, gitDefaultBranch));

  // Print function template parameters (with type, name, default value, and comment) as a list
  if (f.templateParams.size() > 0) {
    main.AddChild(CTML::Node("h4", "Template Parameters"));
    CTML::Node dl("dl");

    for (auto tparam : f.templateParams) {
      auto dt = CTML::Node("dt.is-family-code").AppendRawHTML(escapeForHTML(tparam.type));
      dt.AddChild(CTML::Node("b", " " + tparam.name));

      if (tparam.defaultValue != "") {
        dt.AppendText(" = " + tparam.defaultValue);
      }
      dl.AddChild(dt);
      if (tparam.docComment != "") {
        dl.AddChild(CTML::Node("dd", tparam.docComment));
      }
    }
    main.AddChild(dl);
  }

  // Print function parameters (with type, name, default value, and comment) as a list
  if (f.params.size() > 0) {
    main.AddChild(CTML::Node("h4", "Parameters"));
    CTML::Node dl("dl");

    for (auto param : f.params) {
      auto dt = CTML::Node("dt.is-family-code").AppendRawHTML(getHyperlinkedTypeName(param.type));
      dt.AddChild(CTML::Node("b", " " + param.name));

      if (param.defaultValue != "") {
        dt.AppendText(" = " + param.defaultValue);
      }
      dl.AddChild(dt);
      if (param.docComment != "") {
        dl.AddChild(CTML::Node("dd", param.docComment));
      }
    }
    main.AddChild(dl);
  }

  // Return value description
  if (f.returnTypeDocComment != "") {
    main.AddChild(CTML::Node("h4", "Returns"));
    main.AddChild(CTML::Node("p", f.returnTypeDocComment));
  }

  main.AddChild(CTML::Node("hr.member-fun-separator"));
}

/// Print all of the functions that aren't record members in a project
void hdoc::serde::HTMLWriter::printFunctions() const {
  CTML::Node main("main");
  main.AddChild(CTML::Node("h1", "Functions"));

  // get and sort the list of freestanding function groups
  std::vector<types::FreestandingFunctionID> sortedFunctionGroups;
  for (const auto& [id, group] : this->index->freestandingFunctions) {
    sortedFunctionGroups.push_back(id);
  }
  // sort by detail forst, then name
  std::sort(sortedFunctionGroups.begin(), sortedFunctionGroups.end(), [&](const auto& a, const auto& b) {
    const auto& fa = this->index->freestandingFunctions.at(a);
    const auto& fb = this->index->freestandingFunctions.at(b);
    if (fa.isDetail != fb.isDetail) {
      return fb.isDetail;
    }
    return a.name < b.name;
  });

  // Print a bullet list of functions
  uint64_t   numFunctions = 0; // Number of functions that aren't methods
  CTML::Node ul("ul");
  for (const auto& id : sortedFunctionGroups) {
    const auto& funs = this->index->freestandingFunctions.at(id);
    numFunctions += 1;
    auto li = CTML::Node("li")
                    .AddChild(CTML::Node("a.is-family-code", id.name).SetAttr("href", getFunctionGroupURL(id, true)))
                    .AppendText(getSymbolBlurb(funs, *this->index));
    if (funs.isDetail) li.ToggleClass("hdoc-detail");
    ul.AddChild(li);
    CTML::Node page("main");
    this->pool.async(
        [&](const hdoc::types::FreestandingFunction& func, CTML::Node pg) {
          for (const auto individualFunctionID : func.functionIDs) {
            printFunction(this->index->functions.entries.at(individualFunctionID),
                          pg,
                          this->cfg->gitRepoURL,
                          this->cfg->gitDefaultBranch);
          }
          // use first symbol for breadcrumb, it doesn't matter
          const auto firstSymbol = this->index->functions.entries.at(func.functionIDs.front());
          printNewPage(*this->cfg,
                       pg,
                       this->cfg->outputDir / getFunctionGroupURL(id, false),
                       "function " + id.name + ": " + this->cfg->getPageTitleSuffix(),
                       getBreadcrumbNode("function", firstSymbol, *this->index));
        },
        funs,
        page);
  }
  this->pool.wait();
  main.AddChild(CTML::Node("h2", "Overview"));
  if (numFunctions == 0) {
    main.AddChild(CTML::Node("p", "No functions were declared in this project."));
  } else {
    main.AddChild(ul);
  }
  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / entryPageUrl<types::FunctionSymbol>(true),
               "Functions: " + this->cfg->getPageTitleSuffix());
}

static std::string getAliasHTML(const hdoc::types::AliasSymbol& a) {
  auto str = fmt::format("{} = {};", a.proto, a.target.name);
  str = hdoc::serde::clangFormat(str);
  str = escapeForHTML(str);
  return str;
}

/// Print an alias to main
void hdoc::serde::HTMLWriter::printAlias(const hdoc::types::AliasSymbol& a,
                                         CTML::Node&                     main,
                                         const std::string_view          gitRepoURL,
                                         const std::string_view          gitDefaultBranch) const {
  auto        inner = CTML::Node("code.hdoc-function-code.language-cpp").AppendRawHTML(getAliasHTML(a));
  main.AddChild(CTML::Node("h3#" + a.ID.str())
                    .AddChild(CTML::Node("pre.p-0.hdoc-pre-parent")
                                  .AddChild(CTML::Node("a.is-size-4", "¶")
                                                .SetAttr("class", "hdoc-permalink-icon")
                                                .SetAttr("href", "#" + a.ID.str()))
                                  .AddChild(inner)));

  // Print description only if there's an associated comment
  if (a.briefComment != "" || a.docComment != "") {
    main.AddChild(CTML::Node("h4", "Description"));
  }
  appendAsMarkdown(a.briefComment, main);
  appendAsMarkdown(a.docComment, main);

  main.AddChild(getDeclaredAtNode(a, gitRepoURL, gitDefaultBranch));

  // Print template parameters (with type, name, default value, and comment) as a list
  // TODO this is duplicated from printRecord()
  if (a.templateParams.size() > 0) {
    main.AddChild(CTML::Node("h2", "Template Parameters"));
    CTML::Node dl("dl");

    for (auto tparam : a.templateParams) {
      auto dt = CTML::Node("dt.is-family-code").AppendRawHTML(tparam.type);
      dt.AddChild(CTML::Node("b", " " + tparam.name));

      if (tparam.defaultValue != "") {
        dt.AppendText(" = " + tparam.defaultValue);
      }
      dl.AddChild(dt);
      if (tparam.docComment != "") {
        dl.AddChild(CTML::Node("dd", tparam.docComment));
      }
    }
    main.AddChild(dl);
  }

  // If we have a symbol, link it
  if (a.target.id.raw() != 0) {
    main.AddChild(CTML::Node("h4", "Target"));
    main.AddChild(CTML::Node("p", "The target of this alias is ").AppendRawHTML(getHyperlinkedTypeName(a.target)));
  }
}

/// Print all of the aliases that aren't record members in a project
void hdoc::serde::HTMLWriter::printAliases() const {
  CTML::Node main("main");
  main.AddChild(CTML::Node("h1", "Aliases"));

  // Print a bullet list of usings
  uint64_t   numUsings = 0; // Number of usings that aren't methods
  CTML::Node ul("ul");
  for (const auto& id : getSortedIDs(map2vec(this->index->aliases), this->index->aliases)) {
    const auto& u = this->index->aliases.entries.at(id);
    if (u.isRecordMember) {
      continue;
    }
    numUsings += 1;
    auto li = CTML::Node("li")
                    .AddChild(CTML::Node("a.is-family-code", u.name).SetAttr("href", u.relativeUrl()))
                    .AppendText(getSymbolBlurb(u));
    if (u.isDetail) li.ToggleClass("hdoc-detail");
    ul.AddChild(li);
    CTML::Node page("main");
    this->pool.async(
        [&](const hdoc::types::AliasSymbol& alias, CTML::Node pg) {
          printAlias(alias, pg, this->cfg->gitRepoURL, this->cfg->gitDefaultBranch);
          printNewPage(*this->cfg,
                       pg,
                       this->cfg->outputDir / alias.url(),
                       "alias " + alias.name + ": " + this->cfg->getPageTitleSuffix(),
                       getBreadcrumbNode("alias", alias, *this->index));
        },
        u,
        page);
  }
  this->pool.wait();
  main.AddChild(CTML::Node("h2", "Overview"));
  if (numUsings == 0) {
    main.AddChild(CTML::Node("p", "No namespace-level aliases were declared in this project."));
  } else {
    main.AddChild(ul);
  }
  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / entryPageUrl<types::AliasSymbol>(true),
               "Aliases: " + this->cfg->getPageTitleSuffix());
}

static std::vector<hdoc::types::RecordSymbol::BaseRecord> getInheritedSymbols(const hdoc::types::Index*        index,
                                                                              const hdoc::types::RecordSymbol& root) {
  std::vector<hdoc::types::RecordSymbol::BaseRecord> vec   = {};
  std::stack<hdoc::types::RecordSymbol::BaseRecord>  stack = {};
  for (const auto& base : root.baseRecords) {
    stack.push(base);
  }

  // Do a depth-first traversal of the parents of root
  while (!stack.empty()) {
    const hdoc::types::RecordSymbol::BaseRecord record = stack.top();
    stack.pop();

    // Quit if the base record is in std namespace
    if (index->records.contains(record.id) == false) {
      continue;
    }

    // Records inherited privately are ignored and their children are not traversed
    // This is suboptimal since an immediate privately inherited parent of root might have some important members
    // we'd like to document; for now I'm not implementing that since that edge case would balloon code complexity
    if (record.access == clang::AS_private) {
      continue;
    }

    vec.emplace_back(record);

    // Add children to stack for traversing
    const auto& c = index->records.entries.at(record.id);
    for (const auto& baseRecord : c.baseRecords) {
      stack.push(baseRecord);
    }
  }
  return vec;
}

void hdoc::serde::HTMLWriter::printMemberVariables(const hdoc::types::RecordSymbol& c,
                                                   CTML::Node&                      main,
                                                   const bool&                      isInherited) const {
  CTML::Node dl("dl");
  uint64_t   numVars = 0;

  // sort member variables by access level
  std::vector<hdoc::types::MemberVariable> sortedVars = c.vars;
  std::stable_sort(sortedVars.begin(), sortedVars.end(), [](const hdoc::types::MemberVariable& a, const hdoc::types::MemberVariable& b) {
    return a.access < b.access;
  });

  for (const hdoc::types::MemberVariable& var : sortedVars) {
    if (isInherited == true && var.access == clang::AS_private) {
      continue;
    }

    std::string preamble = var.isStatic ? " static " : " ";

    CTML::Node dt;
    // Print the access, type, name, and doc comment if it exists
    if (isInherited == false) {
      dt     = CTML::Node("dt.is-family-code").AppendRawHTML(preamble + " " + getHyperlinkedTypeName(var.type) + " ");
      auto b = CTML::Node("b", var.name);
      dt.AddChild(b);
      dt.SetAttr("id", "var_" + var.name);
    }
    // Inherited variables get a bullet point and link to the description in the parent record
    else {
      dt = CTML::Node("dt.is-family-code");
      const auto a =
          CTML::Node("a", preamble).SetAttr("href", c.relativeUrl() + "#var_" + var.name).AddChild(CTML::Node("b", var.name));
      dt.AddChild(a);
    }
    if (var.defaultValue != "") {
      dt.AppendText(" = " + var.defaultValue);
    }

    if(var.access == clang::AS_protected) dt.ToggleClass("hdoc-protected");
    if(var.access == clang::AS_private) dt.ToggleClass("hdoc-private");

    dl.AddChild(dt);

    if (isInherited == false && var.docComment != "") {
      dl.AddChild(CTML::Node("dd", var.docComment));
    }

    numVars += 1;
  }

  if (numVars > 0) {
    if (isInherited) {
      main.AddChild(CTML::Node("p", "Inherited from ")
                        .AddChild(CTML::Node("a", c.name).SetAttr("href", c.relativeUrl()))
                        .AppendText(":"));
    }
    main.AddChild(dl);
  }
}

/// Print a list of inherited methods for the given record, truncating the method declaration
static void
printInheritedMethods(const hdoc::types::Index* index, const hdoc::types::RecordSymbol& c, CTML::Node& main) {
  auto ul = CTML::Node("ul");

  for (const auto& methodID : getSortedIDs(c.methodIDs, index->functions)) {
    const auto& f = index->functions.entries.at(methodID);
    // Skip private functions and ctors/dtors that aren't inherited
    if (f.access == clang::AS_private || f.isCtorOrDtor) {
      continue;
    }

    const auto li = CTML::Node("li.is-family-code")
                        .AddChild(CTML::Node("a", to_string(f.access) + " ")
                                      .SetAttr("href", c.relativeUrl() + "#" + f.ID.str())
                                      .AddChild(CTML::Node("b", f.name)));
    ul.AddChild(li);
  }

  if (c.methodIDs.size() > 0) {
    main.AddChild(
        CTML::Node("p", "Inherited from ").AddChild(CTML::Node("a", c.name).SetAttr("href", c.relativeUrl())).AppendText(":"));
    main.AddChild(ul);
  }
}

static CTML::Node printFunctionOverview(const std::vector<hdoc::types::SymbolID>& ids,
                                        const hdoc::types::Index&                 index) {
  CTML::Node ul("ul");
  for (auto fnID : ids) {
    const hdoc::types::FunctionSymbol m = index.functions.entries.at(fnID);

    // Divide up the full function declaration so its name can be bold in the HTML
    // and to reformat it for the overview list with trailing return type
    const uint64_t    nameLen      = m.name.size();
    const std::string templatePart = m.proto.substr(0, m.postTemplate);
    std::string       retTypePart  = m.proto.substr(m.postTemplate, m.nameStart - m.postTemplate);
    const std::string inlineMarker = "inline";
    if (retTypePart.starts_with(inlineMarker)) {
      retTypePart = retTypePart.substr(inlineMarker.size());
    }
    hdoc::utils::trim(retTypePart);
    const std::string postName = m.proto.substr(m.nameStart + nameLen, m.proto.size() - m.nameStart - nameLen);

    auto li = CTML::Node("li.is-family-code");
    if (!templatePart.empty())
      li.AddChild(CTML::Node("span.hdoc-overview-template", templatePart)).AppendRawHTML("<br>");
    li.AddChild(CTML::Node("a").SetAttr("href", "#" + m.ID.str()).AddChild(CTML::Node("b", m.name)));
    li.AppendText(postName);
    if (!retTypePart.empty()) li.AppendRawHTML(" &rarr; ").AppendText(retTypePart);
    if (m.access == clang::AS_private) li.ToggleClass("hdoc-private");
    if (m.access == clang::AS_protected) li.ToggleClass("hdoc-protected");
    ul.AddChild(li);
  }
  return ul;
}

/// Print a record to main
void hdoc::serde::HTMLWriter::printRecord(const hdoc::types::RecordSymbol& c) const {
  CTML::Node main("main");

  const std::string pageTitle = c.type + " " + c.name;
  main.AddChild(CTML::Node("h1", pageTitle));

  // Full declaration
  main.AddChild(CTML::Node("h2", "Declaration"));
  main.AddChild(CTML::Node("pre.p-0").AddChild(
      CTML::Node("code.hdoc-record-code.language-cpp",
                 hdoc::serde::clangFormat(c.proto, 70) + " { /* full declaration omitted */ };")));

  if (c.briefComment != "" || c.docComment != "") {
    main.AddChild(CTML::Node("h2", "Description"));
  }
  appendAsMarkdown(c.briefComment, main);
  appendAsMarkdown(c.docComment, main);

  main.AddChild(getDeclaredAtNode(c, this->cfg->gitRepoURL, this->cfg->gitDefaultBranch));

  // Base records
  uint64_t count = 0;
  if (c.baseRecords.size() > 0) {
    auto baseP = CTML::Node("p", "Inherits from: ");
    for (const auto& baseRecord : c.baseRecords) {
      if (count > 0) {
        baseP.AppendText(", ");
      }
      // Check if type is a string, indicating it's a std record that isn't in the DB
      if (this->index->records.contains(baseRecord.id) == false) {
        baseP.AppendText(baseRecord.name);
      } else {
        const auto& p = this->index->records.entries.at(baseRecord.id);
        baseP.AddChild(CTML::Node("a", p.name).SetAttr("href", p.relativeUrl()));
      }
      count++;
    }
    main.AddChild(baseP);
  }

  // Print template parameters (with type, name, default value, and comment) as a list
  if (c.templateParams.size() > 0) {
    main.AddChild(CTML::Node("h2", "Template Parameters"));
    CTML::Node dl("dl");

    for (auto tparam : c.templateParams) {
      auto dt = CTML::Node("dt.is-family-code").AppendRawHTML(tparam.type);
      dt.AddChild(CTML::Node("b", " " + tparam.name));

      if (tparam.defaultValue != "") {
        dt.AppendText(" = " + tparam.defaultValue);
      }
      dl.AddChild(dt);
      if (tparam.docComment != "") {
        dl.AddChild(CTML::Node("dd", tparam.docComment));
      }
    }
    main.AddChild(dl);
  }

  // Print regular member variables
  bool hasMemberVariableHeading = false;
  if (c.vars.size() > 0) {
    main.AddChild(CTML::Node("h2", "Member Variables"));
    hasMemberVariableHeading = true;
    printMemberVariables(c, main, false);
  }

  // Print inherited member variables
  const auto inheritedRecords = getInheritedSymbols(this->index, c);
  for (const auto& base : inheritedRecords) {
    const auto& ic = this->index->records.entries.at(base.id);
    if (hasMemberVariableHeading == false && ic.vars.size() > 0) {
      main.AddChild(CTML::Node("h2", "Member Variables"));
      hasMemberVariableHeading = true;
    }
    printMemberVariables(ic, main, true);
  }

  // Print type aliases
  if(c.aliasIDs.size() > 0) {
    main.AddChild(CTML::Node("h2", "Member Aliases"));
    CTML::Node ul("ul");
    for (const auto& aliasID : getSortedIDs(c.aliasIDs, this->index->aliases)) {
      const auto& a = this->index->aliases.entries.at(aliasID);
      auto li = CTML::Node("li.is-family-code").AppendRawHTML(getAliasHTML(a));
      if(a.access == clang::AS_private) li.ToggleClass("hdoc-private");
      if(a.access == clang::AS_protected) li.ToggleClass("hdoc-protected");
      ul.AddChild(li);
    }
    main.AddChild(ul);
  }

  // Method overview in list form
  const auto& sortedMethodIDs          = getSortedIDs(c.methodIDs, this->index->functions);
  bool        hasMethodOverviewHeading = false;
  if (sortedMethodIDs.size() > 0) {
    main.AddChild(CTML::Node("h2", "Member Function Overview"));
    hasMethodOverviewHeading = true;
    main.AddChild(printFunctionOverview(sortedMethodIDs, *this->index));
  }

  // Add inherited methods to the list
  for (const auto& base : inheritedRecords) {
    const auto& ic = this->index->records.entries.at(base.id);
    if (hasMethodOverviewHeading == false && c.methodIDs.size() > 0) {
      main.AddChild(CTML::Node("h2", "Member Function Overview"));
      hasMethodOverviewHeading = true;
    }
    printInheritedMethods(this->index, ic, main);
  }

  // Hidden-friend function overview in list form
  const auto& sortedHiddenFriendIDs = getSortedIDs(c.hiddenFriendIDs, this->index->functions);
  if (sortedHiddenFriendIDs.size() > 0) {
    main.AddChild(CTML::Node("h2", "Friend Function Overview"));
    main.AddChild(printFunctionOverview(sortedHiddenFriendIDs, *this->index));
  }

  // List of methods with full information
  if (sortedMethodIDs.size() > 0) {
    main.AddChild(CTML::Node("h2", "Member Functions"));
    for (const auto& methodID : sortedMethodIDs) {
      // TODO: get to the bottom of what's causing empty method decls to appear in Writer.hpp
      // For now this hack just avoids printing them, but this shouldn't be necessary
      if (index->functions.contains(methodID) == false) {
        continue;
      }
      printFunction(
          this->index->functions.entries.at(methodID), main, this->cfg->gitRepoURL, this->cfg->gitDefaultBranch);
    }
  }

  // List hidden friend functions with full information
  if (sortedHiddenFriendIDs.size() > 0) {
    main.AddChild(CTML::Node("h2", "Friend Functions"));
    for (const auto& friendID : sortedHiddenFriendIDs) {
      printFunction(
          this->index->functions.entries.at(friendID), main, this->cfg->gitRepoURL, this->cfg->gitDefaultBranch);
    }
  }

  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / c.url(),
               pageTitle + ": " + this->cfg->getPageTitleSuffix(),
               getBreadcrumbNode(c.type, c, *this->index));
}

/// Print all of the records in a project
void hdoc::serde::HTMLWriter::printRecords() const {
  CTML::Node main("main");
  main.AddChild(CTML::Node("h1", "Records"));

  // List of all the records defined, with links to the individual record HTML
  CTML::Node ul("ul");
  for (const auto& id : getSortedIDs(map2vec(this->index->records), this->index->records)) {
    const auto& c = this->index->records.entries.at(id);
    auto li = CTML::Node("li")
                    .AddChild(CTML::Node("a.is-family-code", c.type + " " + c.name).SetAttr("href", c.relativeUrl()))
                    .AppendText(getSymbolBlurb(c));
    if (c.isDetail) li.ToggleClass("hdoc-detail");
    ul.AddChild(li);
    this->pool.async([&](const hdoc::types::RecordSymbol& cls) { printRecord(cls); }, c);
  }
  this->pool.wait();
  main.AddChild(CTML::Node("h2", "Overview"));
  if (this->index->records.entries.size() == 0) {
    main.AddChild(CTML::Node("p", "No records were declared in this project."));
  } else {
    main.AddChild(ul);
  }
  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / entryPageUrl<types::RecordSymbol>(true),
               "Records: " + this->cfg->getPageTitleSuffix());
}

/// Recursively print an single namespace and all of its children
/// Should be tail-call optimized
CTML::Node hdoc::serde::HTMLWriter::printNamespace(const hdoc::types::NamespaceSymbol& ns) const {
  // Base case: stop recursion when namespace has no further children
  // and return an empty node, which will not be appended since we have a custom version of CTML
  if (ns.records.size() == 0 && ns.enums.size() == 0 && ns.namespaces.size() == 0 && ns.usings.size() == 0) {
    return CTML::Node("");
  }

  auto enclosingDetails = CTML::Node("details");
  enclosingDetails.AddChild(CTML::Node("summary.is-family-code#" + ns.ID.str(), ns.name));
  if(!ns.isDetail) {
    enclosingDetails.SetAttr("open", "true");
  }
  auto subUL = CTML::Node("ul");

  const std::vector<hdoc::types::SymbolID> childNamespaces = getSortedIDs(ns.namespaces, index->namespaces);
  const std::vector<hdoc::types::SymbolID> childRecords    = getSortedIDs(ns.records, index->records);
  const std::vector<hdoc::types::SymbolID> childEnums      = getSortedIDs(ns.enums, index->enums);
  const std::vector<hdoc::types::SymbolID> childAliases    = getSortedIDs(ns.usings, index->aliases);
  const std::vector<hdoc::types::SymbolID> childFunctions  = getSortedIDs(ns.functions, index->functions);

  for (const auto& childID : childNamespaces) {
    if (index->namespaces.contains(childID == false)) {
      continue;
    }
    auto childNode = printNamespace(index->namespaces.entries.at(childID));
    subUL.AddChild(childNode);
  }
  for (const auto& childID : childRecords) {
    if (index->records.contains(childID == false)) {
      continue;
    }
    const hdoc::types::RecordSymbol s = index->records.entries.at(childID);
    subUL.AddChild(
        CTML::Node("li.is-family-code").AddChild(CTML::Node("a", s.type + " " + s.name).SetAttr("href", s.relativeUrl())));
  }
  for (const auto& childID : childEnums) {
    if (index->enums.contains(childID == false)) {
      continue;
    }
    const hdoc::types::EnumSymbol s = index->enums.entries.at(childID);
    subUL.AddChild(
        CTML::Node("li.is-family-code").AddChild(CTML::Node("a", s.type + " " + s.name).SetAttr("href", s.relativeUrl())));
  }
  for (const auto& childID : childAliases) {
    if (index->aliases.contains(childID == false)) {
      continue;
    }
    const hdoc::types::AliasSymbol s = index->aliases.entries.at(childID);
    subUL.AddChild(
        CTML::Node("li.is-family-code").AddChild(CTML::Node("a", "using " + s.name).SetAttr("href", s.relativeUrl())));
  }
  // Function groups in this namespace
  std::set<types::FreestandingFunctionID> alreadyIncludedGroups;
  for (const auto& childID : childFunctions) {
    if (index->functions.contains(childID == false)) {
      continue;
    }
    const hdoc::types::FunctionSymbol s = index->functions.entries.at(childID);
    if (s.freestandingID == types::notFreeStanding || alreadyIncludedGroups.contains(s.freestandingID)) {
      continue;
    }
    alreadyIncludedGroups.insert(s.freestandingID);
    subUL.AddChild(
        CTML::Node("li.is-family-code").AddChild(CTML::Node("a", "function " + s.name).SetAttr("href", getFunctionGroupURL(s.freestandingID, true))));
  }
  return enclosingDetails.AddChild(subUL);
}

/// Print all of the namespaces in a project in a nice tree-view
void hdoc::serde::HTMLWriter::printNamespaces() const {
  CTML::Node main("main");
  main.AddChild(CTML::Node("h1", "Namespaces"));

  CTML::Node namespaceTree("ul");

  for (const auto& id : getSortedIDs(map2vec(this->index->namespaces), this->index->namespaces)) {
    const auto& ns = this->index->namespaces.entries.at(id);
    // Only recurse root namespaces (that have no parents)
    if (ns.parentNamespaceID.raw() != 0) {
      continue;
    }
    namespaceTree.AddChild(printNamespace(ns));
  }
  if (this->index->namespaces.entries.size() == 0) {
    main.AddChild(CTML::Node("p", "No namespaces were declared in this project."));
  } else {
    main.AddChild(namespaceTree);
  }
  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / entryPageUrl<types::NamespaceSymbol>(true),
               "Namespaces: " + this->cfg->getPageTitleSuffix());
}

/// Print an enum to main
void hdoc::serde::HTMLWriter::printEnum(const hdoc::types::EnumSymbol& e) const {
  CTML::Node        main("main");
  const std::string pageTitle = e.type + " " + e.name;
  main.AddChild(CTML::Node("h1", pageTitle));

  // Description
  if (e.briefComment != "" || e.docComment != "") {
    main.AddChild(CTML::Node("h2", "Description"));
  }
  appendAsMarkdown(e.briefComment, main);
  appendAsMarkdown(e.docComment, main);

  main.AddChild(getDeclaredAtNode(e, this->cfg->gitRepoURL, this->cfg->gitDefaultBranch));

  // Enum members in table format
  main.AddChild(CTML::Node("h2", "Enumerators"));
  if (e.members.size() > 0) {
    // Table and table header nodes
    CTML::Node table("table.table is-narrow is-hoverable");
    CTML::Node table_header_row("tr");
    table_header_row.AddChild(CTML::Node("th", "Name"));
    table_header_row.AddChild(CTML::Node("th", "Value"));
    table_header_row.AddChild(CTML::Node("th", "Comment"));
    table.AddChild(table_header_row);

    // Table rows: one row per enum member
    for (const auto& member : e.members) {
      CTML::Node table_row("tr");
      table_row.AddChild(CTML::Node("td.is-family-code", member.name));
      table_row.AddChild(CTML::Node("td.is-family-code", std::to_string(member.value)));
      table_row.AddChild(CTML::Node("td", member.docComment));
      table.AddChild(table_row);
    }
    main.AddChild(table);
  }

  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / e.url(),
               pageTitle + ": " + this->cfg->getPageTitleSuffix(),
               getBreadcrumbNode(e.type, e, *this->index));
}

/// Print all of the enums in a project
void hdoc::serde::HTMLWriter::printEnums() const {
  CTML::Node main("main");
  main.AddChild(CTML::Node("h1", "Enums"));

  CTML::Node ul("ul");
  for (const auto& id : getSortedIDs(map2vec(this->index->enums), this->index->enums)) {
    const auto& e = this->index->enums.entries.at(id);
    auto li = CTML::Node("li")
                    .AddChild(CTML::Node("a.is-family-code", e.type + " " + e.name).SetAttr("href", e.relativeUrl()))
                    .AppendText(getSymbolBlurb(e));
    if (e.isDetail) li.ToggleClass("hdoc-detail");
    ul.AddChild(li);
    this->pool.async([&](const hdoc::types::EnumSymbol& en) { printEnum(en); }, e);
  }
  this->pool.wait();
  main.AddChild(CTML::Node("h2", "Overview"));
  if (this->index->enums.entries.size() == 0) {
    main.AddChild(CTML::Node("p", "No enums were declared in this project."));
  } else {
    main.AddChild(ul);
  }
  printNewPage(*this->cfg,
               main,
               this->cfg->outputDir / entryPageUrl<types::EnumSymbol>(true),
               "Enums: " + this->cfg->getPageTitleSuffix());
}

void hdoc::serde::HTMLWriter::printSearchPage() const {
  CTML::Node main("main");

  main.AddChild(CTML::Node("h1", "Search"));
  const auto noscriptTagText = R"(Search requires Javascript to be enabled.
No data leaves your machine as part of the search process.
We have left the Javascript code unminified so that you are able to inspect it yourself should you choose to do so.)";
  main.AddChild(CTML::Node("noscript").AddChild(CTML::Node("p", noscriptTagText)));
  const auto input = CTML::Node("input.input is-primary#search")
                         .SetAttr("type", "search")
                         .SetAttr("autocomplete", "off")
                         .SetAttr("onkeyup", "updateSearchResults()")
                         .SetAttr("style", "display: none");
  main.AddChild(input);
  main.AddChild(CTML::Node("div#loader").AddChild(CTML::Node("span.loader")));
  main.AddChild(CTML::Node("p#info", "Loading index of all symbols. This may take time for large codebases."));
  main.AddChild(CTML::Node("div.panel is-hoverable#results").SetAttr("style", "display: none"));
  main.AddChild(CTML::Node("script").SetAttr("src", "index.min.js"));
  main.AddChild(CTML::Node("script").SetAttr("src", "search.js"));
  printNewPage(*this->cfg, main, this->cfg->outputDir / "search.html", "Search: " + this->cfg->getPageTitleSuffix());

  std::error_code      ec;
  llvm::raw_fd_ostream jsonPath((cfg->outputDir / "index.json").string(), ec);
  llvm::json::OStream  json(jsonPath);

  json.array([&] {
    for (const auto& s : this->index->functions.entries)
      json.object([&] {
        auto& f = s.second;
        const auto listAsMember = f.isRecordMember || f.isHiddenFriend;
        json.attribute("sid", listAsMember ? f.parentNamespaceID.str() + ".html#" + f.ID.str() : f.ID.str());
        json.attribute("name", f.name);
        json.attribute("decl", f.proto);
        json.attribute("type", listAsMember ? 0 : 1);
      });

    for (const auto& s : this->index->records.entries) {
      json.object([&] {
        auto& c = s.second;
        json.attribute("sid", c.ID.str());
        json.attribute("name", c.name);
        json.attribute("decl", c.proto);
        if (c.type == "struct") {
          json.attribute("type", 2);
        } else if (c.type == "class") {
          json.attribute("type", 3);
        } else {
          json.attribute("type", 4);
        }
      });
    }

    for (const auto& s : this->index->enums.entries) {
      json.object([&] {
        auto& e = s.second;
        json.attribute("sid", e.ID.str());
        json.attribute("name", e.name);
        json.attribute("decl", e.name);
        json.attribute("type", 5);
      });

      for (const auto& ev : s.second.members) {
        json.object([&] {
          auto& e = s.second;
          json.attribute("sid", e.ID.str());
          json.attribute("name", ev.name);
          json.attribute("decl", e.name + "::" + ev.name);
          json.attribute("type", 6);
        });
      }
    }
  });
}

/// Print the homepage of the documentation
void hdoc::serde::HTMLWriter::printProjectIndex() const {
  CTML::Node main("main");

  // If index markdown page was supplied, convert it to markdown and print it
  if (this->cfg->homepage != "") {
    hdoc::utils::MarkdownConverter converter(this->cfg->homepage);
    main = converter.getHTMLNode();
  }
  // Otherwise, create a simple page with links to the documentation
  else {
    main.AddChild(CTML::Node("h1", this->cfg->getPageTitleSuffix()));
    CTML::Node ul("ul");
    appendEntryPageLinks(ul, true);
    main.AddChild(ul);
  }

  printNewPage(*this->cfg, main, this->cfg->outputDir / "index.html", this->cfg->getPageTitleSuffix(), CTML::Node(), true);
}

void hdoc::serde::HTMLWriter::processMarkdownFiles() const {
  for (const auto& f : this->cfg->mdPaths) {
    spdlog::info("Processing markdown file {}", f.string());
    hdoc::utils::MarkdownConverter converter(f);
    CTML::Node                     main      = converter.getHTMLNode();
    std::string                    filename  = "doc" + f.filename().replace_extension("html").string();
    std::string                    pageTitle = f.filename().stem().string();
    printNewPage(*this->cfg, main, this->cfg->outputDir / filename, pageTitle, CTML::Node(), true);
  }
}

std::string hdoc::serde::HTMLWriter::getNamespaceString(const hdoc::types::SymbolID& n) const {
  std::vector<std::string> nsNames;
  auto ns = n;
  while(ns != 0) {
    const auto& nss = this->index->namespaces.entries.at(ns);
    nsNames.push_back(nss.name);
    ns = nss.parentNamespaceID;
  }
  std::reverse(nsNames.begin(), nsNames.end());
  std::string ret = "";
  for(size_t i = 0; i < nsNames.size(); i++) {
    ret += nsNames[i];
    if(i != nsNames.size() - 1) {
      ret += "_";
    }
  }
  return ret;
}

std::string getRecordUrl(const hdoc::types::SymbolID& id, bool relative) {
  return (relative?"../":"") + hdoc::types::RecordSymbol().directory() + "/" + id.str() + ".html";
};
std::string getEnumURL(const hdoc::types::SymbolID& id, bool relative) {
  return (relative?"../":"") + hdoc::types::EnumSymbol().directory() + "/" + id.str() + ".html";
};
std::string getAliasURL(const hdoc::types::SymbolID& id, bool relative) {
  return (relative?"../":"") + hdoc::types::AliasSymbol().directory() + "/" + id.str() + ".html";
};

std::string hdoc::serde::HTMLWriter::getURLForSymbol(const hdoc::types::SymbolID& id, bool relative) const {
  if (index->records.contains(id)) {
    return getRecordUrl(id, relative);
  } else if (index->enums.contains(id)) {
    return getEnumURL(id, relative);
  } else if (index->aliases.contains(id)) {
    return getAliasURL(id, relative);
  } else {
    return "";
  }
}

std::string hdoc::serde::HTMLWriter::getFunctionURL(const hdoc::types::SymbolID& f, bool relative) const {
  const auto& func = this->index->functions.entries.at(f);
  if(func.freestandingID != types::notFreeStanding) {
    // this function is printed in a page for its group
    return getFunctionGroupURL(func.freestandingID, relative) + "#" + func.ID.str();
  }
  else {
    // this function is part of its record
    const auto record = this->index->records.entries.at(func.parentNamespaceID);
    if(relative) {
      return record.relativeUrl() + "#" + func.ID.str();
    }
    else {
      return record.url() + "#" + func.ID.str();
    }
  }
}

std::string hdoc::serde::HTMLWriter::getFunctionGroupURL(const hdoc::types::FreestandingFunctionID& f, bool relative) const {
  const auto namespaceStr = getNamespaceString(f.parentNamespaceID);
  if(relative) {
    return "../functions/" + namespaceStr + "-" + f.name + ".html";
  }
  else {
    return "functions/" + namespaceStr + "-" + f.name + ".html";
  }
}
