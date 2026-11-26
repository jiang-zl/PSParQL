#include <raptor2/raptor2.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

namespace {
using Dictionary = unordered_map<string, int>;
using Graph = unordered_map<int, vector<pair<int, int>>>; // subject -> (object, predicate)

int intern(Dictionary &dictionary, const string &term)
{
  auto found = dictionary.find(term);
  if (found != dictionary.end()) return found->second;
  const int id = static_cast<int>(dictionary.size()) + 1;
  dictionary.emplace(term, id);
  return id;
}

void skip_space(const string &line, size_t &pos)
{
  while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos]))) ++pos;
}

void read_delimited(const string &line, size_t &pos, char end)
{
  ++pos;
  while (pos < line.size())
  {
    char ch = line[pos++];
    if (ch == end) return;
    if (ch == '\\' && pos < line.size()) ++pos;
  }
  throw runtime_error("unterminated RDF term");
}

// Triple-pattern files also allow variables and an optional final dot.
string read_term(const string &line, size_t &pos)
{
  skip_space(line, pos);
  const size_t begin = pos;
  if (pos == line.size() || line[pos] == '#') throw runtime_error("missing term");
  if (line[pos] == '<')
    read_delimited(line, pos, '>');
  else if (line[pos] == '"')
  {
    read_delimited(line, pos, '"');
    if (line.compare(pos, 3, "^^<") == 0)
    {
      pos += 2;
      read_delimited(line, pos, '>');
    }
    else if (pos < line.size() && line[pos] == '@')
    {
      ++pos;
      while (pos < line.size() && (isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '-')) ++pos;
    }
  }
  else
  {
    while (pos < line.size() && !isspace(static_cast<unsigned char>(line[pos])) && line[pos] != '#') ++pos;
    if (pos > begin && line[pos - 1] == '.') --pos;
  }
  if (pos == begin) throw runtime_error("empty term");
  return line.substr(begin, pos - begin);
}

string term_key(raptor_world *world, string token)
{
  // Preserve legacy bare identifiers. N-Triples escapes, language tags and
  // datatypes are interpreted by Raptor for both data and query constants.
  if (token[0] != '<' && token[0] != '"' && token.compare(0, 2, "_:") != 0)
    return token;
  unique_ptr<raptor_term, decltype(&raptor_free_term)> term(
      raptor_new_term_from_counted_string(world, reinterpret_cast<unsigned char *>(&token[0]), token.size()),
      raptor_free_term);
  if (!term) throw runtime_error("invalid RDF term: " + token);
  size_t size;
  unique_ptr<unsigned char, decltype(&raptor_free_memory)> bytes(
      raptor_term_to_counted_string(term.get(), &size), raptor_free_memory);
  if (!bytes) throw runtime_error("could not encode RDF term");
  string key(reinterpret_cast<const char *>(bytes.get()), size);
  static const string string_type = "^^<http://www.w3.org/2001/XMLSchema#string>";
  if (term->type == RAPTOR_TERM_TYPE_LITERAL && key.size() >= string_type.size() &&
      key.compare(key.size() - string_type.size(), string_type.size(), string_type) == 0)
    key.resize(key.size() - string_type.size());
  return key;
}

int intern_term(Dictionary &dictionary, raptor_world *world, const string &token)
{
  // Most input terms are already in canonical N-Triples form. Reuse their
  // IDs before asking the RDF parser to decode another occurrence.
  auto found = dictionary.find(token);
  if (found != dictionary.end()) return found->second;
  return intern(dictionary, term_key(world, token));
}

struct Encoder
{
  raptor_world *world;
  Dictionary vertices, predicates, variables;
  size_t triples = 0;

  int vertex(const string &token, bool query)
  {
    if (query && (token[0] == '?' || token.compare(0, 2, "_:") == 0))
      return -intern(variables, token);
    if (token[0] == '?') throw runtime_error("variable in data graph");
    return intern_term(vertices, world, token);
  }

  void convert(const string &source, const string &target, bool query)
  {
    ifstream input(source);
    if (!input) throw runtime_error("cannot open " + source);
    Graph graph;
    string line;
    size_t number = 0;
    while (getline(input, line))
    {
      ++number;
      size_t pos = 0;
      skip_space(line, pos);
      if (pos == line.size() || line[pos] == '#') continue;
      try
      {
        const int subject = vertex(read_term(line, pos), query);
        string p = read_term(line, pos);
        if (p[0] == '?') throw runtime_error("predicate variables are unsupported");
        const int predicate = intern_term(predicates, world, p);
        const int object = vertex(read_term(line, pos), query);
        skip_space(line, pos);
        if (pos < line.size() && line[pos] == '.') ++pos;
        skip_space(line, pos);
        if (pos < line.size() && line[pos] != '#') throw runtime_error("unexpected text after object");
        graph[subject].emplace_back(object, predicate);
      }
      catch (const runtime_error &error)
      {
        throw runtime_error(source + ":" + to_string(number) + ": " + error.what());
      }
    }
    if (input.bad()) throw runtime_error("read failed: " + source);

    ofstream output(target);
    if (!output) throw runtime_error("cannot open " + target);
    for (auto &entry : graph)
    {
      auto &edges = entry.second;
      sort(edges.begin(), edges.end());
      edges.erase(unique(edges.begin(), edges.end()), edges.end());
      output << entry.first << ' ' << entry.first << '\t';
      for (const auto &edge : edges)
        output << edge.first << ' ' << edge.first << ' ' << edge.second << ' ';
      output << '\n';
      if (!query) triples += edges.size();
    }
    output.close();
    if (!output) throw runtime_error("write failed: " + target);
  }
};
}

int main(int argc, char **argv)
{
  if (argc != 1 && (argc < 3 || argc % 2 == 0))
  {
    cerr << "usage: data_proc DATA DATA_ADJ [QUERY QUERY_ADJ ...]\n";
    return 2;
  }
  unique_ptr<raptor_world, decltype(&raptor_free_world)> world(raptor_new_world(), raptor_free_world);
  if (!world || raptor_world_open(world.get())) return 1;
  Encoder encoder;
  encoder.world = world.get();
  try
  {
    if (argc == 1)
    {
      encoder.convert("YAGO2.txt", "yago/YAGO2.txt", false);
      for (int i = 1; i <= 4; ++i)
      {
        string file = "YAGO_Q" + to_string(i) + ".txt";
        encoder.convert("yagoquery/" + file, "yago/" + file, true);
      }
    }
    else
    {
      for (int i = 1; i < argc; i += 2)
        encoder.convert(argv[i], argv[i + 1], i != 1);
    }
    cout << "Entities: " << encoder.vertices.size() << ", Triples: " << encoder.triples << '\n';
  }
  catch (const exception &error)
  {
    cerr << error.what() << '\n';
    return 1;
  }
}
