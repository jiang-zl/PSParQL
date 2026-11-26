"""Exercise the standalone converter using the same term dictionary as the query."""
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def read_edges(path):
    edges = []
    for line in path.read_text().splitlines():
        subject, label, *adj = map(int, line.split())
        assert subject == label
        for obj, obj_label, predicate in zip(adj[::3], adj[1::3], adj[2::3]):
            assert obj == obj_label
            edges.append((subject, predicate, obj))
    return edges


def main(converter):
    with TemporaryDirectory() as tmp:
        directory = Path(tmp)
        data = directory / "graph.nt"
        query = directory / "patterns.txt"
        data_adj = directory / "graph.adj"
        query_adj = directory / "query.adj"
        names = [r'"Mary Ann"', r'"Mary Jane"', r'"Mary Ann"@en',
                 r'"Mary Ann"^^<urn:custom>', r'"an escaped \"quote\" # inside"']
        data.write_text("".join(f"<urn:a> <urn:p> {name} .\n" for name in names) +
                        r'<urn:a> <urn:p> "M\u0061ry Ann" .' + "\n" +
                        '<urn:a> <urn:p> "Mary Ann"@EN . # comment\n' +
                        '<urn:a> <urn:p> "Mary Ann"^^<http://www.w3.org/2001/XMLSchema#string> .\n' +
                        '<urn:a> <urn:q> <urn:b> .\n<urn:b> <urn:q> <urn:a> .\n')
        query.write_text("".join(f"?person <urn:p> {name} .\n" for name in names))
        run = [converter, data, data_adj, query, query_adj]
        subprocess.run(run, check=True)
        facts = read_edges(data_adj)
        patterns = read_edges(query_adj)
        assert len(facts) == len(set(facts)) == 7
        assert len(patterns) == 5 and len({s for s, p, o in patterns}) == 1
        assert all(s < 0 for s, p, o in patterns)
        assert all(any((p, o) == (pred, obj) for _, pred, obj in facts)
                   for _, p, o in patterns)
        query.write_text('?person <urn:p> "unterminated\n')
        assert subprocess.run(run, capture_output=True).returncode != 0


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()))
