#include "base.hpp"
#include "scan.hpp"
#include "table.hpp"

#include <iostream>

using namespace std;
using namespace fn;

int entry(int argc, const char **argv) {
    if (argc < 2) {
        cout << "argument required\n";
        return -1;
    }

    istringstream in(argv[1]);
    fn_scan::Scanner sc(&in);

    auto tok = sc.nextToken();
    while (tok.tk != fn_scan::TKEOF) {
        cout << tok.to_string() << endl;
        tok = sc.nextToken();
    }

    // test of hash table
    Table<int> testTab(1);
    testTab.insert("test", 4);
    testTab.insert("test2", 5);
    testTab.insert("test3", 6);
    auto res = testTab.get("test2");
    if (res==nullptr) {
        cout << "at test idx: " << "nothing" << endl;
    } else {
        cout << "at test idx: " << *res << endl;
    }
    

    return 0;
}

extern "C" {
int main (int argc, const char **argv) {
    return entry(argc, argv);
}
};

