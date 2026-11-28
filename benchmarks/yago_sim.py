"""Generate one simulated RDF graph and encode the four ecrc/rdf-exp YAGO2 BGPs."""
import argparse
from collections import defaultdict
from pathlib import Path
import subprocess


def generate(cohorts, students, cast, films):
    facts = set()
    for c in range(cohorts):
        for m in range(4):
            advisor = f"Advisor{c}_{m}"
            city = f"City{c}_{m%2}"
            facts.add((advisor,"wasBornIn",city))
            for i in range(students):
                person, spouse = f"Student{c}_{m}_{i}", f"Spouse{c}_{m}_{i}"
                birthplace = city if i%4 else f"City{c}_other"
                facts.update([(person,"hasAcademicAdvisor",advisor),
                              (person,"hasGivenName",f'"Given {i%19}"'),
                              (person,"hasFamilyName",f'"Family {m}_{i%11}"'),
                              (person,"hasPreferredName",f'"Student {c} {m} {i}"'),
                              (spouse,"hasPreferredName",f'"Spouse {c} {m} {i}"'),
                              (person,"isMarriedTo",spouse),
                              (person,"wasBornIn",birthplace),
                              (spouse,"wasBornIn",birthplace if i%3 else f"Other{c}"),
                              (person,"livesIn",f"Other{c}")])
                if i%8==0:
                    facts.add((person,"hasGivenName",f'"Alias {i}"'))
                    facts.add((person,"hasFamilyName",f'"New Family {i}"'))
        for a in range(cast):
            actor = f"Actor{c}_{a}"
            facts.update((actor,"hasPreferredName",f'"Artist {c} {a} Name {k}"') for k in range(5))
            facts.update((actor,"actedIn",f"Film{c}_{f}") for f in range(films))
        # Negative controls and one-actor films; unrelated names influence the
        # plan only via the same real predicate frequencies used by production.
        solo = f"Solo{c}"
        facts.update([(solo,"hasPreferredName",f'"Solo {c}"'),(solo,"actedIn",f"Short{c}")])
        facts.update((f"Extra{c}_{i}","hasPreferredName",f'"Other Name {i}"') for i in range(cast*films))
    return facts


def count(facts):
    by = defaultdict(lambda: defaultdict(set))
    for s,p,o in facts: by[p][s].add(o)
    born, given, family, names = [by[k] for k in ("wasBornIn","hasGivenName","hasFamilyName","hasPreferredName")]
    q1=q2=q4=0
    for p, advisors in by["hasAcademicAdvisor"].items():
        weight = len(given[p])*len(family[p])
        for a in advisors:
            cities = born[p] & born[a]
            q1 += weight*len(cities)
            for spouse in by["isMarriedTo"][p]:
                q2 += weight*len(cities & born[spouse])
    for p, spouses in by["isMarriedTo"].items():
        for s in spouses:
            q4 += len(names[p])*len(names[s])*len(born[p] & born[s])
    cast = defaultdict(int)
    for a, films in by["actedIn"].items():
        for f in films: cast[f] += len(names[a])
    return [q1,q2,sum(n*n for n in cast.values()),q4]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory",type=Path)
    p.add_argument("converter",type=Path)
    p.add_argument("--cohorts",type=int,default=8)
    p.add_argument("--students",type=int,default=64)
    p.add_argument("--cast",type=int,default=256)
    p.add_argument("--films",type=int,default=8)
    a=p.parse_args();d=a.directory
    facts=generate(a.cohorts,a.students,a.cast,a.films)
    d.mkdir(parents=True,exist_ok=True)
    with (d/"data.nt").open("w") as stream:
        for s,p,o in sorted(facts):
            term=o if o.startswith('"') else f"<urn:sim:{o}>"
            print(f"<urn:sim:{s}> <http://yago-knowledge.org/resource/{p}> {term} .",file=stream)
    cmd=[a.converter.resolve(),d/"data.nt",d/"data"]
    for i,expected in enumerate(count(facts),1):
        raw=(Path(__file__).parent/"queries"/"yago2"/f"{i}.rq").read_text()
        path=d/f"q{i}.patterns"
        path.write_text("".join(line.strip()+"\n" for line in raw.splitlines() if line.strip().startswith("?")))
        cmd.extend([path,d/f"q{i}"])
        (d/f"expected{i}").write_text(f"{expected}\n")
        print(f"Q{i}",expected)
    subprocess.run(cmd,check=True)
    print("Distinct triples",len(facts))

if __name__=="__main__": main()
