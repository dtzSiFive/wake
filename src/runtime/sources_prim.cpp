/*
 * Copyright 2026 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sources_prim.h"

#include <charconv>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "database.h"
#include "prim.h"
#include "types/data.h"
#include "value.h"

namespace {

// Build a PathInfo-shaped nested-Pair tuple matching parseCachedPathInfo's expected shape:
// (name, (type, (hash, (mode, mtimeNs)))).
size_t reserve_path_info(const FileReflection &f) {
  return reserve_tuple5() + String::reserve(f.path.size()) + String::reserve(f.type.size()) +
         String::reserve(f.hash.size()) + Integer::reserve(f.mode) +
         Integer::reserve(f.modified);
}

Value *claim_path_info(Heap &h, const FileReflection &f) {
  return claim_tuple5(h, String::claim(h, f.path), String::claim(h, f.type),
                      String::claim(h, f.hash), Integer::claim(h, MPZ(f.mode)),
                      Integer::claim(h, MPZ(f.modified)));
}

// Splits a string_view on '\0', yielding non-empty views into the original buffer.
std::vector<std::string_view> split_null(std::string_view buf) {
  std::vector<std::string_view> parts;
  size_t start = 0;
  size_t pos;
  while ((pos = buf.find('\0', start)) != std::string_view::npos) {
    if (pos > start) parts.push_back(buf.substr(start, pos - start));
    start = pos + 1;
  }
  if (start < buf.size()) parts.push_back(buf.substr(start));
  return parts;
}

}  // namespace

// prim "sources_lookup" linesArg -> Result (Pair (List PathInfo) (List String)) String
// linesArg: newline-separated "path\tmtimeNs" entries.
// Returns Pass(Pair hits misses): hits is a List of PathInfo tuples (name, type, hash, mode,
// mtimeNs), misses is a List of paths. Order matches input order within each list (hits in input
// order, then misses in input order).
static PRIMTYPE(type_sources_lookup) {
  // PathInfo: 5 nested Pairs (name, (type, (hash, (mode, mtimeNs)))).
  TypeVar p1, p2, p3, p4;
  Data::typePair.clone(p1);
  Data::typePair.clone(p2);
  Data::typePair.clone(p3);
  Data::typePair.clone(p4);
  p1[0].unify(Data::typeString);
  p1[1].unify(p2);
  p2[0].unify(Data::typeString);
  p2[1].unify(p3);
  p3[0].unify(Data::typeString);
  p3[1].unify(p4);
  p4[0].unify(Data::typeInteger);
  p4[1].unify(Data::typeInteger);

  TypeVar hitList, missList;
  Data::typeList.clone(hitList);
  Data::typeList.clone(missList);
  hitList[0].unify(p1);
  missList[0].unify(Data::typeString);

  TypeVar pair;
  Data::typePair.clone(pair);
  pair[0].unify(hitList);
  pair[1].unify(missList);

  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(pair);
  result[1].unify(Data::typeString);

  return args.size() == 1 && args[0]->unify(Data::typeString) && out->unify(result);
}

static PRIMFN(prim_sources_lookup) {
  Database *db = static_cast<Database *>(data);
  EXPECT(1);
  STRING(lines_arg, 0);

  auto fail = [&](const std::string &msg) {
    runtime.heap.reserve(reserve_result() + String::reserve(msg.size()));
    auto err = String::claim(runtime.heap, msg);
    RETURN(claim_result(runtime.heap, false, err));
  };

  auto tokens = split_null({lines_arg->c_str(), lines_arg->size()});
  if (tokens.size() % 2 != 0) {
    fail("sources_lookup: expected even token count, got " + std::to_string(tokens.size()));
    return;
  }

  std::vector<std::pair<std::string, int64_t>> path_mtimes;
  path_mtimes.reserve(tokens.size() / 2);
  for (size_t i = 0; i < tokens.size(); i += 2) {
    int64_t mtime;
    auto sv = tokens[i + 1];
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), mtime);
    if (ec != std::errc{}) {
      fail("sources_lookup: invalid mtime: " + std::string(sv));
      return;
    }
    path_mtimes.emplace_back(std::string(tokens[i]), mtime);
  }

  std::vector<FileReflection> hits;
  std::vector<std::string> misses;
  db->lookup_sources(path_mtimes, hits, misses);

  // Reserve heap for the full result.
  size_t need = reserve_result() + reserve_tuple2() + reserve_list(hits.size()) +
                reserve_list(misses.size());
  for (const auto &h : hits) need += reserve_path_info(h);
  for (const auto &m : misses) need += String::reserve(m.size());
  runtime.heap.reserve(need);

  std::vector<Value *> hit_values;
  hit_values.reserve(hits.size());
  for (const auto &h : hits) hit_values.push_back(claim_path_info(runtime.heap, h));

  std::vector<Value *> miss_values;
  miss_values.reserve(misses.size());
  for (const auto &m : misses) miss_values.push_back(String::claim(runtime.heap, m));

  Value *hit_list = claim_list(runtime.heap, hit_values.size(), hit_values.data());
  Value *miss_list = claim_list(runtime.heap, miss_values.size(), miss_values.data());
  Value *pair = claim_tuple2(runtime.heap, hit_list, miss_list);
  RETURN(claim_result(runtime.heap, true, pair));
}

// Helper: parse a flat '\0'-separated "path\0type\0hash\0mode\0mtimeNs\0..." token stream into
// FileReflections. Each record is exactly 5 tokens. Returns true on success; on failure populates
// err and returns false.
static bool parse_register_lines(std::string_view buf, std::vector<FileReflection> &out,
                                 std::string &err) {
  auto tokens = split_null(buf);
  if (tokens.size() % 5 != 0) {
    err = "expected token count divisible by 5, got " + std::to_string(tokens.size());
    return false;
  }
  out.reserve(tokens.size() / 5);
  for (size_t i = 0; i < tokens.size(); i += 5) {
    long mode;
    int64_t mtime;
    {
      auto sv = tokens[i + 3];
      auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), mode);
      if (ec != std::errc{}) {
        err = "invalid mode in record " + std::to_string(i / 5) + ": " + std::string(sv);
        return false;
      }
    }
    {
      auto sv = tokens[i + 4];
      auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), mtime);
      if (ec != std::errc{}) {
        err = "invalid mtime in record " + std::to_string(i / 5) + ": " + std::string(sv);
        return false;
      }
    }
    out.emplace_back(std::string(tokens[i]), std::string(tokens[i + 1]),
                     std::string(tokens[i + 2]), mode, mtime);
  }
  return true;
}

// prim "sources_register_files" linesArg -> Result Unit String
// linesArg: newline-separated "path\ttype\thash\tmode\tmtimeNs" entries.
// Inserts each entry into files (insert-or-ignore) and run_files (claim_file) so the file_id is
// pinned for this run. No filetree, no synthetic job. Caller is expected to follow up with CAS
// ingestion for any newly-staged blobs, then call sources_register_filetree.
static PRIMTYPE(type_sources_register_files) {
  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(Data::typeUnit);
  result[1].unify(Data::typeString);
  return args.size() == 1 && args[0]->unify(Data::typeString) && out->unify(result);
}

static PRIMFN(prim_sources_register_files) {
  Database *db = static_cast<Database *>(data);
  EXPECT(1);
  STRING(lines_arg, 0);

  auto fail = [&](const std::string &msg) {
    runtime.heap.reserve(reserve_result() + String::reserve(msg.size()));
    auto err = String::claim(runtime.heap, msg);
    RETURN(claim_result(runtime.heap, false, err));
  };

  std::vector<FileReflection> entries;
  std::string parse_err;
  if (!parse_register_lines({lines_arg->c_str(), lines_arg->size()}, entries, parse_err)) {
    fail("sources_register_files: " + parse_err);
    return;
  }

  db->register_sources_files(entries);

  runtime.heap.reserve(reserve_result() + reserve_unit());
  RETURN(claim_result(runtime.heap, true, claim_unit(runtime.heap)));
}

// prim "sources_register_filetree" cmdArg linesArg -> Result (List PathInfo) String
// cmdArg: the canonical commandline string used as the synthetic job's cmd.
// linesArg: same shape as sources_register_files. Creates a synthetic source-aggregator job,
// links each entry's already-registered file_id via filetree(access=2). Returns Pass(List
// PathInfo) on success.
static PRIMTYPE(type_sources_register_filetree) {
  TypeVar p1, p2, p3, p4;
  Data::typePair.clone(p1);
  Data::typePair.clone(p2);
  Data::typePair.clone(p3);
  Data::typePair.clone(p4);
  p1[0].unify(Data::typeString);
  p1[1].unify(p2);
  p2[0].unify(Data::typeString);
  p2[1].unify(p3);
  p3[0].unify(Data::typeString);
  p3[1].unify(p4);
  p4[0].unify(Data::typeInteger);
  p4[1].unify(Data::typeInteger);

  TypeVar list;
  Data::typeList.clone(list);
  list[0].unify(p1);

  TypeVar result;
  Data::typeResult.clone(result);
  result[0].unify(list);
  result[1].unify(Data::typeString);

  return args.size() == 2 && args[0]->unify(Data::typeString) &&
         args[1]->unify(Data::typeString) && out->unify(result);
}

static PRIMFN(prim_sources_register_filetree) {
  Database *db = static_cast<Database *>(data);
  EXPECT(2);
  STRING(cmd_arg, 0);
  STRING(lines_arg, 1);

  auto fail = [&](const std::string &msg) {
    runtime.heap.reserve(reserve_result() + String::reserve(msg.size()));
    auto err = String::claim(runtime.heap, msg);
    RETURN(claim_result(runtime.heap, false, err));
  };

  std::vector<FileReflection> entries;
  std::string parse_err;
  if (!parse_register_lines({lines_arg->c_str(), lines_arg->size()}, entries, parse_err)) {
    fail("sources_register_filetree: " + parse_err);
    return;
  }

  std::vector<FileReflection> registered;
  db->register_sources_filetree(std::string(cmd_arg->c_str(), cmd_arg->size()), entries,
                                registered);

  size_t need = reserve_result() + reserve_list(registered.size());
  for (const auto &r : registered) need += reserve_path_info(r);
  runtime.heap.reserve(need);

  std::vector<Value *> values;
  values.reserve(registered.size());
  for (const auto &r : registered) values.push_back(claim_path_info(runtime.heap, r));

  Value *list = claim_list(runtime.heap, values.size(), values.data());
  RETURN(claim_result(runtime.heap, true, list));
}

void prim_register_sources_prim(Database *db, PrimMap &pmap) {
  prim_register(pmap, "sources_lookup", prim_sources_lookup, type_sources_lookup, PRIM_IMPURE, db);
  prim_register(pmap, "sources_register_files", prim_sources_register_files,
                type_sources_register_files, PRIM_IMPURE, db);
  prim_register(pmap, "sources_register_filetree", prim_sources_register_filetree,
                type_sources_register_filetree, PRIM_IMPURE, db);
}
