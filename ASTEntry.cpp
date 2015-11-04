/*
 See ASTEntry.h
*/

#include "ASTEntry.h"

#include "BinaryAddressMap.h"
#include "Renaming.h"

#include "clang/AST/AST.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/SaveAndRestore.h"

#include "third-party/picojson-1.3.0/picojson.h"

#include <string>

std::string escape_for_json(const std::string & s)
{
  std::string ans;
  for (size_t i = 0; i < s.size(); ++i) {
    switch (s[i]) {
    case '\n':
      ans.append("\\n");
      break;
    case '\"':
      ans.append("\\\"");
      break;
    case '\\':
      ans.append("\\\\");
      break;
    default:
      ans.push_back(s[i]);
    }
  }
  return ans;
}

std::string unescape_from_json(const std::string & s)
{
  std::string ans;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i != s.size() - 1) {
      char c = s[++i];
      if (c == 'n')
        ans.push_back('\n');
      else
        ans.push_back(c);
      continue;
    }
    ans.push_back(s[i]);
  }
  return ans;
}

picojson::value renames_to_json(const Renames & renames, RenameKind k)
{
  std::vector<picojson::value> ans;
  for (Renames::const_iterator it = renames.begin();
       it != renames.end();
       ++it)
  {
      if (it->kind == k) {
          ans.push_back(picojson::value(it->name));
      }
  }
  return picojson::value(ans);
}

void json_to_renames(const picojson::value & jv,
                     RenameKind k,
                     Renames & renames)
{
  if (!jv.is<picojson::array>()) {
    assert (!"expected a json array");
    return;
  }

  std::vector<picojson::value> vals = jv.get<picojson::array>();
  for (std::vector<picojson::value>::iterator it = vals.begin();
       it != vals.end();
       ++it)
  {
      renames.insert(RenameDatum(NULL, it->get<std::string>(), k));
  }
}

namespace clang_mutate
{
  ASTEntry* ASTEntryFactory::make( const picojson::value &jsonValue )
  {
    if ( ASTBinaryEntry::jsonObjHasRequiredFields(jsonValue) )
      return new ASTBinaryEntry(jsonValue);
    else if ( ASTNonBinaryEntry::jsonObjHasRequiredFields(jsonValue) )
      return new ASTNonBinaryEntry(jsonValue);
    else
      return NULL;
  }

  ASTEntry* ASTEntryFactory::make(
      const unsigned int counter,
      clang::Stmt *s,
      clang::Rewriter& rewrite,
      BinaryAddressMap &binaryAddressMap,
      const Renames & renames)
  {
    clang::SourceManager &sm = rewrite.getSourceMgr();
    clang::PresumedLoc beginLoc = sm.getPresumedLoc(s->getSourceRange().getBegin());
    clang::PresumedLoc endLoc = sm.getPresumedLoc(s->getSourceRange().getEnd());

    std::string srcFileName = sm.getFileEntryForID( sm.getMainFileID() )->getName();
    unsigned int beginSrcLine = beginLoc.getLine();
    unsigned int endSrcLine = endLoc.getLine();

    if ( binaryAddressMap.canGetBeginAddressForLine( srcFileName, beginSrcLine) &&
         binaryAddressMap.canGetEndAddressForLine( srcFileName, endSrcLine ) )
    {
      return new ASTBinaryEntry( counter,
                                 s,
                                 rewrite,
                                 binaryAddressMap,
                                 renames);
    }
    else
    {
      return new ASTNonBinaryEntry( counter,
                                    s,
                                    rewrite,
                                    renames);
    }
  }

  ASTNonBinaryEntry::ASTNonBinaryEntry() :
    m_counter(0),
    m_astClass(""),
    m_srcFileName(""),
    m_beginSrcLine(0),
    m_beginSrcCol(0),
    m_endSrcLine(0),
    m_endSrcCol(0),
    m_srcText(""),
    m_renames()
  {
  }

  ASTNonBinaryEntry::ASTNonBinaryEntry( 
    const unsigned int counter,
    const std::string &astClass,
    const std::string &srcFileName,
    const unsigned int beginSrcLine,
    const unsigned int beginSrcCol,
    const unsigned int endSrcLine,
    const unsigned int endSrcCol,
    const std::string &srcText,
    const Renames & renames) :

    m_counter(counter),
    m_astClass(astClass),
    m_srcFileName(srcFileName),
    m_beginSrcLine(beginSrcLine),
    m_beginSrcCol(beginSrcCol),
    m_endSrcLine(endSrcLine),
    m_endSrcCol(endSrcCol),
    m_srcText(srcText),
    m_renames(renames)
  {
  }

  ASTNonBinaryEntry::ASTNonBinaryEntry( 
    const int counter,
    clang::Stmt * s,
    clang::Rewriter& rewrite,
    const Renames & renames)
  {
    clang::SourceManager &sm = rewrite.getSourceMgr();
    clang::PresumedLoc beginLoc = sm.getPresumedLoc(s->getSourceRange().getBegin());
    clang::PresumedLoc endLoc = sm.getPresumedLoc(s->getSourceRange().getEnd());

    clang::FileID main_id = sm.getMainFileID();
    
    m_counter = counter;
    m_astClass = s->getStmtClassName();
    m_srcFileName = sm.getFileEntryForID(main_id)->getName();
    m_beginSrcLine = beginLoc.getLine();
    m_beginSrcCol = beginLoc.getColumn();
    m_endSrcLine = endLoc.getLine();
    m_endSrcCol = endLoc.getColumn();
    m_renames = renames;
    
    RenameFreeVar renamer(s, rewrite, renames);
    m_srcText = renamer.getRewrittenString();
  }

  ASTNonBinaryEntry::ASTNonBinaryEntry( const picojson::value &jsonValue )
  {
    if ( ASTNonBinaryEntry::jsonObjHasRequiredFields(jsonValue) )
    {
      m_counter      = jsonValue.get("counter").get<int64_t>();
      m_astClass     = jsonValue.get("ast_class").get<std::string>();
      m_srcFileName  = jsonValue.get("src_file_name").get<std::string>();
      m_beginSrcLine = jsonValue.get("begin_src_line").get<int64_t>();
      m_beginSrcCol  = jsonValue.get("begin_src_col").get<int64_t>();
      m_endSrcLine   = jsonValue.get("end_src_line").get<int64_t>();
      m_endSrcCol    = jsonValue.get("end_src_col").get<int64_t>();
      m_srcText      = unescape_from_json(
                          jsonValue.get("src_text").get<std::string>());
      json_to_renames(jsonValue.get("unbound_vals"), VariableRename, m_renames);
      json_to_renames(jsonValue.get("unbound_funs"), FunctionRename, m_renames);
    }
  }

  ASTNonBinaryEntry::~ASTNonBinaryEntry() {}

  ASTNonBinaryEntry::ASTEntry* ASTNonBinaryEntry::clone() const 
  {
    return new ASTNonBinaryEntry( m_counter,
                                  m_astClass,
                                  m_srcFileName,
                                  m_beginSrcLine,
                                  m_beginSrcCol,
                                  m_endSrcLine,
                                  m_endSrcCol,
                                  m_srcText,
                                  m_renames );
  }

  unsigned int ASTNonBinaryEntry::getCounter() const
  {
    return m_counter;
  }

  std::string ASTNonBinaryEntry::getASTClass() const 
  {
    return m_astClass;
  }

  std::string ASTNonBinaryEntry::getSrcFileName() const
  {
    return m_srcFileName;
  }

  unsigned int ASTNonBinaryEntry::getBeginSrcLine() const
  {
    return m_beginSrcLine;
  }

  unsigned int ASTNonBinaryEntry::getBeginSrcCol() const
  {
    return m_beginSrcCol;
  }

  unsigned int ASTNonBinaryEntry::getEndSrcLine() const
  {
    return m_endSrcLine;
  }

  unsigned int ASTNonBinaryEntry::getEndSrcCol() const
  {
    return m_endSrcCol;
  }

  std::string ASTNonBinaryEntry::getSrcText() const
  {
    return m_srcText;
  }

  Renames ASTNonBinaryEntry::getRenames() const
  {
      return m_renames;
  }
    
  std::string ASTNonBinaryEntry::toString() const
  {
    char msg[256];

    sprintf(msg, "%8d %6d:%-3d %6d:%-3d %s", 
                 m_counter,
                 m_beginSrcLine,
                 m_beginSrcCol,
                 m_endSrcLine,
                 m_endSrcCol,
                 m_astClass.c_str());

    return std::string(msg);
  }

  picojson::value ASTNonBinaryEntry::toJSON() const
  {
    picojson::object jsonObj;
    
    jsonObj["counter"] = picojson::value(static_cast<int64_t>(m_counter));
    jsonObj["ast_class"] = picojson::value(m_astClass);
    jsonObj["src_file_name"] = picojson::value(m_srcFileName);
    jsonObj["begin_src_line"] = picojson::value(static_cast<int64_t>(m_beginSrcLine));
    jsonObj["begin_src_col"] = picojson::value(static_cast<int64_t>(m_beginSrcCol));
    jsonObj["end_src_line"] = picojson::value(static_cast<int64_t>(m_endSrcLine));
    jsonObj["end_src_col"] = picojson::value(static_cast<int64_t>(m_endSrcCol));
    jsonObj["src_text"] = picojson::value(escape_for_json(m_srcText));
    jsonObj["unbound_vals"] = renames_to_json(m_renames, VariableRename);
    jsonObj["unbound_funs"] = renames_to_json(m_renames, FunctionRename);

    return picojson::value(jsonObj);
  }

  bool ASTNonBinaryEntry::jsonObjHasRequiredFields( 
    const picojson::value& jsonValue )
  {
    return jsonValue.is<picojson::object>() && 
           jsonValue.contains("counter") &&
           jsonValue.contains("ast_class") &&
           jsonValue.contains("src_file_name") &&
           jsonValue.contains("begin_src_line") &&
           jsonValue.contains("begin_src_col") &&
           jsonValue.contains("end_src_line") &&
           jsonValue.contains("end_src_col") &&
           jsonValue.contains("src_text") &&
           jsonValue.contains("unbound_vals") &&
           jsonValue.contains("unbound_funs");
  }

  ASTBinaryEntry::ASTBinaryEntry() :
    ASTNonBinaryEntry(),
    m_binaryFilePath(""),
    m_beginAddress(0),
    m_endAddress(0)
  {
  }

  ASTBinaryEntry::ASTBinaryEntry( 
    const unsigned int counter, 
    const std::string &astClass,
    const std::string &srcFileName,
    const unsigned int beginSrcLine,
    const unsigned int beginSrcCol,
    const unsigned int endSrcLine,
    const unsigned int endSrcCol,
    const std::string &srcText,
    const Renames & renames,
    const std::string &binaryFileName,
    const unsigned long beginAddress,
    const unsigned long endAddress,
    const std::string &binaryContents ) :

    ASTNonBinaryEntry( counter, 
                       astClass, 
                       srcFileName, 
                       beginSrcLine, 
                       beginSrcCol, 
                       endSrcLine, 
                       endSrcCol, 
                       srcText,
                       renames),
    m_binaryFilePath(binaryFileName),
    m_beginAddress(beginAddress),
    m_endAddress(endAddress),
    m_binaryContents(binaryContents)
  {
  }

  ASTBinaryEntry::ASTBinaryEntry( 
    const unsigned int counter,
    clang::Stmt * s,
    clang::Rewriter& rewrite,
    BinaryAddressMap& binaryAddressMap,
    const Renames & renames) :

    ASTNonBinaryEntry( counter,
                       s,
                       rewrite,
                       renames )
  {
    m_binaryFilePath = binaryAddressMap.getBinaryPath();
    m_beginAddress = 
        binaryAddressMap.getBeginAddressForLine(
            getSrcFileName(), 
            getBeginSrcLine());
    m_endAddress = 
        binaryAddressMap.getEndAddressForLine(
            getSrcFileName(), 
            getEndSrcLine());
    m_binaryContents = 
        binaryAddressMap.getBinaryContentsAsStr(
            m_beginAddress, 
            m_endAddress);
  }

  ASTBinaryEntry::ASTBinaryEntry( const picojson::value& jsonValue ) :
    ASTNonBinaryEntry( jsonValue )
  {
    if ( ASTBinaryEntry::jsonObjHasRequiredFields(jsonValue) )
    {
      m_binaryFilePath = jsonValue.get("binary_file_path").get<std::string>();
      m_beginAddress   = jsonValue.get("begin_addr").get<int64_t>();
      m_endAddress     = jsonValue.get("end_addr").get<int64_t>();
      m_binaryContents = jsonValue.get("binary_contents").get<std::string>();
    }
  }
  
  ASTBinaryEntry::~ASTBinaryEntry() {}

  ASTBinaryEntry::ASTEntry* ASTBinaryEntry::clone() const 
  {
    return new ASTBinaryEntry( getCounter(),
                               getASTClass(),
                               getSrcFileName(),
                               getBeginSrcLine(),
                               getBeginSrcCol(),
                               getEndSrcLine(),
                               getEndSrcCol(),
                               getSrcText(),
                               getRenames(),
                               m_binaryFilePath,
                               m_beginAddress,
                               m_endAddress,
                               m_binaryContents );
  }

  std::string ASTBinaryEntry::getBinaryFilePath() const
  {
    return m_binaryFilePath;
  }

  unsigned long ASTBinaryEntry::getBeginAddress() const
  {
    return m_beginAddress;
  }

  unsigned long ASTBinaryEntry::getEndAddress() const
  {
    return m_endAddress;
  }

  std::string ASTBinaryEntry::getBinaryContents() const
  {
    return m_binaryContents;
  }

  std::string ASTBinaryEntry::toString() const
  {
    char msg[256];

    sprintf(msg, "%8d %6d:%-3d %6d:%-3d %-25s %#016lx %#016lx", 
                 getCounter(),
                 getBeginSrcLine(),
                 getBeginSrcCol(),
                 getEndSrcLine(),
                 getEndSrcCol(),
                 getASTClass().c_str(),
                 m_beginAddress,
                 m_endAddress);

    return std::string(msg);
  }
  
  picojson::value ASTBinaryEntry::toJSON() const
  {
    picojson::object jsonObj = ASTNonBinaryEntry::toJSON().get<picojson::object>();
    
    jsonObj["binary_file_path"] = picojson::value(m_binaryFilePath);
    jsonObj["begin_addr"] = picojson::value(static_cast<int64_t>(m_beginAddress));
    jsonObj["end_addr"] = picojson::value(static_cast<int64_t>(m_endAddress));
    jsonObj["binary_contents"] = picojson::value(m_binaryContents);

    return picojson::value(jsonObj);
  }

  bool ASTBinaryEntry::jsonObjHasRequiredFields( const picojson::value &jsonValue )
  {
    return ASTNonBinaryEntry::jsonObjHasRequiredFields( jsonValue ) &&
           jsonValue.contains("binary_file_path") &&
           jsonValue.contains("begin_addr") &&
           jsonValue.contains("end_addr") &&
           jsonValue.contains("binary_contents");
  }
}