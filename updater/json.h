#ifndef MONOS_UPD_JSON_H
#define MONOS_UPD_JSON_H

// Mini-parseur JSON autonome (objet/tableau/string/nombre/bool/null).
// Suffisant pour lire le manifeste version.json et les reponses de l'API
// GitHub. La valeur renvoyee est un arbre possede par l'appelant : `delete`.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

namespace monupd {
namespace json {

enum Type { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

struct Value {
  Type  type = J_NULL;
  bool  b    = false;
  long long num = 0;
  char* s  = nullptr;          // valeur si J_STR (copie strobee)
  char* key = nullptr;         // cle si membre d'objet
  Value* first = nullptr;      // 1er enfant (J_ARR items / J_OBJ membres)
  Value* next  = nullptr;      // frere suivant (liste chainee)

  ~Value() {
    delete[] s;  delete[] key;
    Value* p = first;
    while(p) { Value* n = p->next; p->next = nullptr; delete p; p = n; }
  }
};

namespace {
  struct P {
    const char* p;
    const char* end;
    const char* err = nullptr;
    P(const char* text) { p = text; end = text + std::strlen(text); }
    void ws() { while(p < end && std::isspace((unsigned char)*p)) ++p; }
    bool match(const char* word) {
      size_t n = std::strlen(word);
      if((size_t)(end - p) < n) return false;
      if(std::memcmp(p, word, n) != 0) return false;
      p += n; return true;
    }
    std::string str() { // apres '"'
      ++p; std::string out;
      while(p < end) {
        char c = *p++;
        if(c == '"') return out;
        if(c == '\\') {
          if(p >= end) break;
          char e = *p++;
          switch(e){
            case '"' : out += '"'; break;
            case '\\': out += '\\'; break;
            case '/' : out += '/'; break;
            case 'b' : out += '\b'; break;
            case 'f' : out += '\f'; break;
            case 'n' : out += '\n'; break;
            case 'r' : out += '\r'; break;
            case 't' : out += '\t'; break;
            case 'u' : {
              unsigned cp = 0;
              for(int i = 0; i < 4; ++i) {
                if(p >= end) break;
                char h = *p++;
                cp <<= 4;
                if(h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                else if(h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                else if(h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                else cp |= 0;
              }
              if(cp < 0x80) out += (char)cp;
              else if(cp < 0x800){ out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
              else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
              break;
            }
            default: out += e; break;
          }
        } else out += c;
      }
      err = "string non terminee"; return out;
    }
    Value* value() {
      ws();
      if(p >= end) { err = "fin inattendue"; return nullptr; }
      char c = *p;
      if(c == '{') return object();
      if(c == '[') return array();
      if(c == '"') {
        Value* v = new Value; v->type = J_STR;
        std::string sres = str(); if(err) { delete v; return nullptr; }
        v->s = new char[sres.size()+1]; std::memcpy(v->s, sres.c_str(), sres.size()+1);
        return v;
      }
      if(std::isdigit((unsigned char)c) || c == '-') {
        Value* v = new Value; v->type = J_NUM;
        char* nend = nullptr;
        v->num = std::strtoll(p, &nend, 10);
        if(nend == p) { err = "nombre invalide"; delete v; return nullptr; }
        p = nend; return v;
      }
      if(match("true"))  { Value* v = new Value; v->type = J_BOOL; v->b = true;  return v; }
      if(match("false")) { Value* v = new Value; v->type = J_BOOL; v->b = false; return v; }
      if(match("null"))  { return new Value; } // J_NULL
      err = "token inattendu";
      return nullptr;
    }
    Value* object() {
      ++p; Value* v = new Value; v->type = J_OBJ;
      ws(); if(p < end && *p == '}') { ++p; return v; }
      while(p < end) {
        ws(); if(p >= end || *p != '"') { err = "cle attendue"; break; }
        std::string key = str(); if(err) break;
        ws(); if(p >= end || *p != ':') { err = "':' attendu"; break; }
        ++p;
        Value* child = value();
        if(!child || err) { if(child) delete child; break; }
        child->key = new char[key.size()+1]; std::memcpy(child->key, key.c_str(), key.size()+1);
        if(v->first) { Value* t = v->first; while(t->next) t = t->next; t->next = child; }
        else v->first = child;
        ws(); if(p < end && *p == ',') { ++p; continue; }
        if(p < end && *p == '}') { ++p; break; }
        break;
      }
      if(err) { delete v; return nullptr; }
      return v;
    }
    Value* array() {
      ++p; Value* v = new Value; v->type = J_ARR;
      ws(); if(p < end && *p == ']') { ++p; return v; }
      while(p < end) {
        Value* child = value();
        if(!child || err) { if(child) delete child; break; }
        if(v->first) { Value* t = v->first; while(t->next) t = t->next; t->next = child; }
        else v->first = child;
        ws(); if(p < end && *p == ',') { ++p; continue; }
        if(p < end && *p == ']') { ++p; break; }
        break;
      }
      if(err) { delete v; return nullptr; }
      return v;
    }
};

Value* parseInternal(const char* text, const char** err) {
  P pp(text);
  Value* v = pp.value();
  if(pp.err && err) *err = pp.err;
  return v;
}
} // namespace

inline Value* parse(const char* text, const char** err = nullptr) {
  const char* e = nullptr;
  Value* v = parseInternal(text, &e);
  if(err) *err = e;
  else if(e) { delete v; return nullptr; }
  return v;
}

inline const Value* get(const Value* obj, const char* key) {
  if(!obj || obj->type != J_OBJ) return nullptr;
  for(const Value* c = obj->first; c; c = c->next)
    if(c->key && std::strcmp(c->key, key) == 0) return c;
  return nullptr;
}
inline const char* str(const Value* v) { return v && v->type == J_STR ? v->s : nullptr; }
inline long long   num(const Value* v) { return v && v->type == J_NUM ? v->num : 0; }
inline bool        boolean(const Value* v) { return v && v->type == J_BOOL ? v->b : false; }

} // namespace json
} // namespace monupd
#endif