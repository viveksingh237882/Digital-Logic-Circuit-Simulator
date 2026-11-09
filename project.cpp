/*
 *=========================================
 * Digital Logic Circuit Simulator 
 * =======================================
 *
 * Concepts demonstrated
 *   - OOP       : abstract Node/Gate base classes,  AndGate, OrGate, ...
 *                 (inheritance + virtual dispatch; Circuit only calls node->compute())

 *   - Output    : automatic truth-table generation
 *   - Bonus     : half adder, full adder, N-bit ripple-carry adder, N-bit ALU,
 *                 all built purely from the primitive gates
 *   - Interface : interactive menu (choose gate / adder / ALU / build your own circuit)
 *
 * Build:  g++ -std=c++17 -O2 -Wall -Wextra -o logic_simulator logic_simulator.cpp
 * Run:    ./logic_simulator          (interactive menu)
 *         ./logic_simulator --demo   (non-interactive demo of everything)
 */

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

/* ---------------------------------------------------------------------------
 Errors
 ---------------------------------------------------------------------------*/
class CircuitError : public runtime_error {
public:
    explicit CircuitError(const string& msg) : runtime_error(msg) {}
};

/* ---------------------------------------------------------------------------
Node hierarchy (OOP: inheritance + polymorphism)
 ---------------------------------------------------------------------------*/
class Node {
public:
    Node(string name, string kind, size_t arity)
        : name(move(name)), kind(move(kind)), inputs(arity, nullptr) {}
    virtual ~Node() = default;


    virtual int compute(const vector<int>& bits) const = 0;

    string name;
    string kind;
    vector<Node*> inputs;  
    int value = 0;
};


class InputNode : public Node {
public:
    explicit InputNode(string name) : Node(move(name), "INPUT", 0) {}
    int compute(const vector<int>&) const override { return value; }
};

class Gate : public Node {
public:
    using Node::Node;
};

class UnaryGate : public Gate {
public:
    UnaryGate(string name, string kind) : Gate(move(name), move(kind), 1) {}
};

class BinaryGate : public Gate {
public:
    BinaryGate(string name, string kind) : Gate(move(name), move(kind), 2) {}
};

class NotGate : public UnaryGate {
public:
    explicit NotGate(string n = "") : UnaryGate(move(n), "NOT") {}
    int compute(const vector<int>& b) const override { return 1 - b[0]; }
};

class BufferGate : public UnaryGate {
public:
    explicit BufferGate(string n = "") : UnaryGate(move(n), "BUF") {}
    int compute(const vector<int>& b) const override { return b[0]; }
};

class AndGate : public BinaryGate {
public:
    explicit AndGate(string n = "") : BinaryGate(move(n), "AND") {}
    int compute(const vector<int>& b) const override { return b[0] & b[1]; }
};

class OrGate : public BinaryGate {
public:
    explicit OrGate(string n = "") : BinaryGate(move(n), "OR") {}
    int compute(const vector<int>& b) const override { return b[0] | b[1]; }
};

class XorGate : public BinaryGate {
public:
    explicit XorGate(string n = "") : BinaryGate(move(n), "XOR") {}
    int compute(const vector<int>& b) const override { return b[0] ^ b[1]; }
};

class NandGate : public BinaryGate {
public:
    explicit NandGate(string n = "") : BinaryGate(move(n), "NAND") {}
    int compute(const vector<int>& b) const override { return 1 - (b[0] & b[1]); }
};

class NorGate : public BinaryGate {
public:
    explicit NorGate(string n = "") : BinaryGate(move(n), "NOR") {}
    int compute(const vector<int>& b) const override { return 1 - (b[0] | b[1]); }
};

class XnorGate : public BinaryGate {
public:
    explicit XnorGate(string n = "") : BinaryGate(move(n), "XNOR") {}
    int compute(const vector<int>& b) const override { return 1 - (b[0] ^ b[1]); }
};

// Factory: create a gate from its textual name ("AND", "xor", ...).
static unique_ptr<Gate> makeGate(const string& typeName) {
    string t;
    for (char ch : typeName) t += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    if (t == "AND")  return make_unique<AndGate>();
    if (t == "OR")   return make_unique<OrGate>();
    if (t == "NOT")  return make_unique<NotGate>();
    if (t == "XOR")  return make_unique<XorGate>();
    if (t == "NAND") return make_unique<NandGate>();
    if (t == "NOR")  return make_unique<NorGate>();
    if (t == "XNOR") return make_unique<XnorGate>();
    if (t == "BUF")  return make_unique<BufferGate>();
    throw CircuitError("Unknown gate type: " + typeName);
}

/* ---------------------------------------------------------------------------
 Circuit (graph + topological sort) Help took form google
 ---------------------------------------------------------------------------*/
class Circuit {
public:
    explicit Circuit(string name = "circuit") : name_(move(name)) {}
    Circuit(const Circuit&) = delete;
    Circuit& operator=(const Circuit&) = delete;

    // ---- construction ----------------------------------------------------
    Node* addInput(const string& name) {
        auto node = make_unique<InputNode>(name);
        Node* raw = registerNode(move(node));
        inputNames_.push_back(name);
        return raw;
    }

    // Typed version:  c.add<AndGate>({a, b}, "name")
    template <class G>
    Node* add(const vector<Node*>& sources, const string& name = "") {
        return attach(make_unique<G>(), sources, name);
    }

    // String version: c.addGate("AND", {a, b})
    Node* addGate(const string& type, const vector<Node*>& sources,
                  const string& name = "") {
        return attach(makeGate(type), sources, name);
    }

    // Re-wire a pin after creation (used to build feedback for the cycle demo).
    void connect(Node* gate, size_t pin, Node* source) {
        if (pin >= gate->inputs.size())
            throw CircuitError(gate->name + " has no input pin " + to_string(pin));
        gate->inputs[pin] = source;
        order_.clear();
        orderValid_ = false;
    }

    void markOutput(const string& label, Node* node) { outputs_.emplace_back(label, node); }

    // ---- topological sort (Kahn's algorithm) -----------------------------
    const vector<Node*>& topologicalOrder() {
        if (orderValid_) return order_;

        unordered_map<const Node*, int> indegree;
        unordered_map<const Node*, vector<Node*>> consumers;
        unordered_set<const Node*> members;
        for (const auto& n : nodes_) {
            indegree[n.get()] = 0;
            members.insert(n.get());
        }

        for (const auto& n : nodes_) {
            for (size_t pin = 0; pin < n->inputs.size(); ++pin) {
                Node* src = n->inputs[pin];
                if (!src)
                    throw CircuitError("Unconnected pin " + to_string(pin) + " on " + n->name);
                if (!members.count(src))
                    throw CircuitError(n->name + " pin " + to_string(pin) +
                                       " uses a node outside this circuit");
                consumers[src].push_back(n.get());
                ++indegree[n.get()];
            }
        }

        queue<Node*> ready;
        for (const auto& n : nodes_)
            if (indegree[n.get()] == 0) ready.push(n.get());

        vector<Node*> order;
        while (!ready.empty()) {
            Node* cur = ready.front();
            ready.pop();
            order.push_back(cur);
            for (Node* c : consumers[cur])
                if (--indegree[c] == 0) ready.push(c);
        }

        if (order.size() != nodes_.size()) {
            string stuck;
            for (const auto& n : nodes_)
                if (indegree[n.get()] > 0) stuck += (stuck.empty() ? "" : ", ") + n->name;
            throw CircuitError("Cycle detected (feedback loop) involving: " + stuck);
        }

        order_ = move(order);
        orderValid_ = true;
        return order_;
    }

    // ---- simulation ------------------------------------------------------
    // Evaluate for one assignment; returns {output label -> bit}.
    unordered_map<string, int> evaluate(const unordered_map<string, int>& assignment) {
        for (const auto& name : inputNames_) {
            auto it = assignment.find(name);
            if (it == assignment.end()) throw CircuitError("Missing input value: " + name);
            if (it->second != 0 && it->second != 1)
                throw CircuitError("Input " + name + " must be 0 or 1");
            byName_.at(name)->value = it->second;
        }

        vector<int> bits;
        for (Node* node : topologicalOrder()) {  // dependencies always come first
            bits.clear();
            for (Node* src : node->inputs) bits.push_back(src->value);
            node->value = node->compute(bits);   // virtual dispatch (polymorphism)
        }

        unordered_map<string, int> result;
        for (const auto& [label, node] : outputs_) result[label] = node->value;
        return result;
    }

    void printTruthTable() {
        const size_t n = inputNames_.size();
        ostringstream header;
        for (size_t i = 0; i < n; ++i) header << setw(3) << inputNames_[i] << (i + 1 < n ? " | " : "");
        header << " || ";
        for (size_t i = 0; i < outputs_.size(); ++i)
            header << setw(3) << outputs_[i].first << (i + 1 < outputs_.size() ? " | " : "");

        cout << "\nTruth table: " << name_ << "\n" << header.str() << "\n"
                  << string(header.str().size(), '-') << "\n";

        for (unsigned row = 0; row < (1u << n); ++row) {
            unordered_map<string, int> asg;
            ostringstream line;
            for (size_t i = 0; i < n; ++i) {
                int bit = (row >> (n - 1 - i)) & 1;  // first input is the MSB of the row index
                asg[inputNames_[i]] = bit;
                line << setw(3) << bit << (i + 1 < n ? " | " : "");
            }
            auto out = evaluate(asg);
            line << " || ";
            for (size_t i = 0; i < outputs_.size(); ++i)
                line << setw(3) << out[outputs_[i].first] << (i + 1 < outputs_.size() ? " | " : "");
            cout << line.str() << "\n";
        }
    }

    void describe() {
        cout << "\nEvaluation order for '" << name_ << "':\n  ";
        const auto& order = topologicalOrder();
        for (size_t i = 0; i < order.size(); ++i)
            cout << order[i]->name << (i + 1 < order.size() ? " -> " : "\n");
    }

    const string& name() const { return name_; }
    size_t size() const { return nodes_.size(); }
    const vector<string>& inputNames() const { return inputNames_; }

    vector<string> outputLabels() const {
        vector<string> labels;
        for (const auto& o : outputs_) labels.push_back(o.first);
        return labels;
    }

    // Look up a node by name; returns nullptr if it does not exist.
    Node* find(const string& nodeName) const {
        auto it = byName_.find(nodeName);
        return it == byName_.end() ? nullptr : it->second;
    }

private:
    Node* registerNode(unique_ptr<Node> node) {
        if (byName_.count(node->name)) throw CircuitError("Duplicate node name: " + node->name);
        Node* raw = node.get();
        byName_[raw->name] = raw;
        nodes_.push_back(move(node));
        orderValid_ = false;
        return raw;
    }

    Node* attach(unique_ptr<Gate> gate, const vector<Node*>& sources, string name) {
        if (name.empty()) name = gate->kind + "_" + to_string(++autoId_);
        gate->name = move(name);
        if (sources.size() != gate->inputs.size())
            throw CircuitError(gate->kind + " needs " + to_string(gate->inputs.size()) +
                               " inputs, got " + to_string(sources.size()));
        size_t pin = 0;
        for (Node* s : sources) gate->inputs[pin++] = s;
        return registerNode(move(gate));
    }

    string name_;
    vector<unique_ptr<Node>> nodes_;                  // owns all nodes
    unordered_map<string, Node*> byName_;
    vector<string> inputNames_;
    vector<pair<string, Node*>> outputs_;        // ordered
    vector<Node*> order_;
    bool orderValid_ = false;
    int autoId_ = 0;
};

/* ---------------------------------------------------------------------------
 Reusable building blocks (composed only from primitive gates)
 ---------------------------------------------------------------------------*/
using NodePair = pair<Node*, Node*>;

static NodePair halfAdder(Circuit& c, Node* a, Node* b, const string& tag = "HA") {
    Node* sum = c.add<XorGate>({a, b}, tag + "_sum");
    Node* carry = c.add<AndGate>({a, b}, tag + "_carry");
    return {sum, carry};
}

static NodePair fullAdder(Circuit& c, Node* a, Node* b, Node* cin, const string& tag = "FA") {
    auto [s1, c1] = halfAdder(c, a, b, tag + "_h1");
    auto [s2, c2] = halfAdder(c, s1, cin, tag + "_h2");
    Node* carryOut = c.add<OrGate>({c1, c2}, tag + "_cout");
    return {s2, carryOut};
}

// Returns a when sel = 0, b when sel = 1.
static Node* mux2(Circuit& c, Node* sel, Node* a, Node* b, const string& tag) {
    Node* nsel = c.add<NotGate>({sel}, tag + "_nsel");
    Node* t0 = c.add<AndGate>({a, nsel}, tag + "_t0");
    Node* t1 = c.add<AndGate>({b, sel}, tag + "_t1");
    return c.add<OrGate>({t0, t1}, tag + "_out");
}

/* ---------------------------------------------------------------------------
 Ready-made circuits
 ---------------------------------------------------------------------------*/
static unique_ptr<Circuit> buildHalfAdder() {
    auto c = make_unique<Circuit>("Half Adder");
    Node* a = c->addInput("A");
    Node* b = c->addInput("B");
    auto [s, cy] = halfAdder(*c, a, b);
    c->markOutput("SUM", s);
    c->markOutput("CARRY", cy);
    return c;
}

static unique_ptr<Circuit> buildFullAdder() {
    auto c = make_unique<Circuit>("Full Adder");
    Node* a = c->addInput("A");
    Node* b = c->addInput("B");
    Node* cin = c->addInput("CIN");
    auto [s, carryOut] = fullAdder(*c, a, b, cin);
    c->markOutput("SUM", s);
    c->markOutput("COUT", carryOut);
    return c;
}

static unique_ptr<Circuit> buildRippleAdder(int n = 4) {
    auto c = make_unique<Circuit>(to_string(n) + "-bit Ripple-Carry Adder");
    vector<Node*> a, b;
    for (int i = 0; i < n; ++i) a.push_back(c->addInput("A" + to_string(i)));
    for (int i = 0; i < n; ++i) b.push_back(c->addInput("B" + to_string(i)));
    Node* carry = c->addInput("CIN");
    for (int i = 0; i < n; ++i) {
        auto [s, cy] = fullAdder(*c, a[i], b[i], carry, "FA" + to_string(i));
        carry = cy;
        c->markOutput("S" + to_string(i), s);
    }
    c->markOutput("COUT", carry);
    return c;
}

enum AluOp { ALU_ADD = 0, ALU_SUB = 1, ALU_AND = 2, ALU_XOR = 3 };
static const char* aluOpName(int op) {
    static const char* names[] = {"ADD", "SUB", "AND", "XOR"};
    return names[op];
}

/*
  n-bit ALU.  Select lines (S1 S0):  00 ADD   01 SUB   10 AND   11 XOR
  Subtraction = two's complement:  A + (B XOR sub) + sub.
  Outputs: R0..R(n-1), COUT (arithmetic ops only), ZERO.
 */
static unique_ptr<Circuit> buildAlu(int n = 4) {
    auto c = make_unique<Circuit>(to_string(n) + "-bit ALU");
    vector<Node*> a, b;
    for (int i = 0; i < n; ++i) a.push_back(c->addInput("A" + to_string(i)));
    for (int i = 0; i < n; ++i) b.push_back(c->addInput("B" + to_string(i)));
    Node* s0 = c->addInput("S0");
    Node* s1 = c->addInput("S1");

    Node* ns1 = c->add<NotGate>({s1}, "nS1");
    Node* sub = c->add<AndGate>({s0, ns1}, "SUB_EN");  // S1 = 0 and S0 = 1

    Node* carry = sub;  // carry-in = 1 when subtracting
    vector<Node*> results;
    for (int i = 0; i < n; ++i) {
        const string k = to_string(i);
        Node* bx = c->add<XorGate>({b[i], sub}, "Bx" + k);
        auto [arith, cy] = fullAdder(*c, a[i], bx, carry, "FA" + k);
        carry = cy;
        Node* andI = c->add<AndGate>({a[i], b[i]}, "AND" + k);
        Node* xorI = c->add<XorGate>({a[i], b[i]}, "XOR" + k);
        Node* logic = mux2(*c, s0, andI, xorI, "LMUX" + k);   // S0 selects AND / XOR
        Node* res = mux2(*c, s1, arith, logic, "RMUX" + k);   // S1 selects arithmetic / logic
        results.push_back(res);
        c->markOutput("R" + k, res);
    }

    c->markOutput("COUT", c->add<AndGate>({carry, ns1}, "COUT_GATED"));

    Node* anyBit = results[0];
    for (int i = 1; i < n; ++i) anyBit = c->add<OrGate>({anyBit, results[i]}, "ZOR" + to_string(i));
    c->markOutput("ZERO", c->add<NotGate>({anyBit}, "ZERO_FLAG"));
    return c;
}

/* ---------------------------------------------------------------------------
 Word-level helpers
 ---------------------------------------------------------------------------*/
using Assignment = unordered_map<string, int>;

static void loadWord(Assignment& asg, const string& prefix, unsigned value, int n) {
    for (int i = 0; i < n; ++i) asg[prefix + to_string(i)] = (value >> i) & 1u;  // LSB first
}

static unsigned readWord(const Assignment& out, const string& prefix, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; ++i) v |= static_cast<unsigned>(out.at(prefix + to_string(i))) << i;
    return v;
}

struct AdderResult { 
    unsigned sum; 
    int cout;
 };
static AdderResult runAdder(Circuit& c, unsigned x, unsigned y, int cin = 0, int n = 4) {
    Assignment asg;
    loadWord(asg, "A", x, n);
    loadWord(asg, "B", y, n);
    asg["CIN"] = cin;
    auto out = c.evaluate(asg);
    return {readWord(out, "S", n), out.at("COUT")};
}

struct AluResult { unsigned result; int carry; int zero; };
static AluResult runAlu(Circuit& c, unsigned x, unsigned y, int op, int n = 4) {
    Assignment asg;
    loadWord(asg, "A", x, n);
    loadWord(asg, "B", y, n);
    asg["S0"] = op & 1;
    asg["S1"] = (op >> 1) & 1;
    auto out = c.evaluate(asg);
    return {readWord(out, "R", n), out.at("COUT"), out.at("ZERO")};
}

/* ---------------------------------------------------------------------------
 Self-verification: exhaustive comparison against native C++ arithmetic
 ---------------------------------------------------------------------------*/
static void require(bool cond, const string& msg) {
    if (!cond) throw runtime_error("VERIFICATION FAILED: " + msg);
}

static void verifyAll() {
    const int n = 4;
    const unsigned mask = 0xF;

    auto adder = buildRippleAdder(n);
    int adderCases = 0;
    for (unsigned x = 0; x < 16; ++x)
        for (unsigned y = 0; y < 16; ++y)
            for (int cin = 0; cin < 2; ++cin, ++adderCases) {
                auto r = runAdder(*adder, x, y, cin, n);
                unsigned expected = x + y + static_cast<unsigned>(cin);
                require(r.sum == (expected & mask) && r.cout == static_cast<int>(expected >> n),
                        "adder " + to_string(x) + "+" + to_string(y));
            }

    auto alu = buildAlu(n);
    int aluCases = 0;
    for (unsigned x = 0; x < 16; ++x)
        for (unsigned y = 0; y < 16; ++y)
            for (int op = 0; op < 4; ++op, ++aluCases) {
                auto r = runAlu(*alu, x, y, op, n);
                unsigned exp = 0;
                int cy = 0;
                switch (op) {
                    case ALU_ADD: exp = (x + y) & mask; cy = static_cast<int>((x + y) >> n); break;
                    case ALU_SUB: exp = (x - y) & mask; cy = (x >= y) ? 1 : 0; break;  // carry = no borrow
                    case ALU_AND: exp = x & y; break;
                    case ALU_XOR: exp = x ^ y; break;
                }
                string ctx = string(aluOpName(op)) + "(" + to_string(x) + "," + to_string(y) + ")";
                require(r.result == exp, ctx + " result");
                require(r.carry == cy, ctx + " carry");
                require(r.zero == (exp == 0 ? 1 : 0), ctx + " zero");
            }

    cout << "\n[OK] Verified: 4-bit adder (" << adderCases << " cases) and 4-bit ALU ("
              << aluCases << " cases) match native C++ arithmetic.\n";
}

/* ---------------------------------------------------------------------------
 Demo
 ---------------------------------------------------------------------------*/
static string bin4(unsigned v) {
    string s;
    for (int i = 3; i >= 0; --i) s += ((v >> i) & 1u) ? '1' : '0';
    return s;
}

static void demo() {
    // 1. Primitive gate truth tables via the generic (string) API
    for (const char* g : {"AND", "OR", "XOR", "NAND"}) {
        Circuit c(string(g) + " gate");
        Node* a = c.addInput("A");
        Node* b = c.addInput("B");
        c.markOutput("OUT", c.addGate(g, {a, b}));
        c.printTruthTable();
    }
    {
        Circuit c("NOT gate");
        c.markOutput("OUT", c.addGate("NOT", {c.addInput("A")}));
        c.printTruthTable();
    }

    // 2. A custom user-wired circuit:  OUT = (A AND B) XOR (NOT C)
    {
        Circuit c("Custom: (A AND B) XOR (NOT C)");
        Node* a = c.addInput("A");
        Node* b = c.addInput("B");
        Node* cc = c.addInput("C");
        Node* g1 = c.addGate("AND", {a, b}, "g1");
        Node* g2 = c.addGate("NOT", {cc}, "g2");
        c.markOutput("OUT", c.addGate("XOR", {g1, g2}, "g3"));
        c.describe();
        c.printTruthTable();
    }

    // 3. Adders
    buildHalfAdder()->printTruthTable();
    {
        auto fa = buildFullAdder();
        fa->describe();
        fa->printTruthTable();
    }

    // 4. Word-level adder demo
    {
        auto adder = buildRippleAdder(4);
        cout << "\n4-bit ripple-carry adder demo\n";
        const pair<unsigned, unsigned> cases[] = {{3, 5}, {7, 8}, {9, 9}, {15, 1}};
        for (auto [x, y] : cases) {
            auto r = runAdder(*adder, x, y);
            cout << "  " << setw(2) << x << " + " << setw(2) << y << " = " << setw(2) << r.sum
                      << "  (carry out = " << r.cout << ")   [" << bin4(x) << " + " << bin4(y) << " = "
                      << r.cout << bin4(r.sum) << "]\n";
        }
    }

    // 5. ALU demo
    {
        auto alu = buildAlu(4);
        cout << "\n4-bit ALU demo  (" << alu->name() << ": " << alu->size()
                  << " nodes, built only from gates)\n";
        struct Case { unsigned x, y; int op; };
        const Case cases[] = {{6, 3, 0}, {6, 3, 1}, {5, 5, 1}, {12, 10, 2}, {12, 10, 3}};
        for (const auto& k : cases) {
            auto r = runAlu(*alu, k.x, k.y, k.op);
            cout << "  " << aluOpName(k.op) << "(" << setw(2) << k.x << ", " << setw(2) << k.y
                      << ") = " << setw(2) << r.result << "   carry=" << r.carry << " zero=" << r.zero << "\n";
        }
    }

    // 6. Cycle detection
    {
        cout << "\nCycle detection demo\n";
        Circuit loop("Loop");
        Node* a = loop.addInput("A");
        Node* g1 = loop.addGate("AND", {a, a}, "G1");
        Node* g2 = loop.addGate("NOT", {g1}, "G2");
        loop.connect(g1, 1, g2);  // feedback: G2 -> G1
        try {
            loop.evaluate({{"A", 1}});
        } catch (const CircuitError& e) {
            cout << "  Caught expected error: " << e.what() << "\n";
        }
    }

    verifyAll();
}

/* ---------------------------------------------------------------------------
Interactive user interface
 ---------------------------------------------------------------------------*/
struct InputClosed {};  // thrown when stdin reaches end-of-file

static string trim(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static string upperCase(string s) {
    for (char& ch : s) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    return s;
}

static vector<string> splitWords(const string& s) {
    istringstream iss(s);
    vector<string> words;
    string w;
    while (iss >> w) words.push_back(w);
    return words;
}

static string ask(const string& prompt) {
    cout << prompt << flush;
    string s;
    if (!getline(cin, s)) throw InputClosed{};
    return trim(s);
}

static int askInt(const string& prompt, int lo, int hi) {
    while (true) {
        string s = ask(prompt);
        try {
            size_t pos = 0;
            int v = stoi(s, &pos);
            if (pos == s.size() && v >= lo && v <= hi) return v;
        } catch (...) {
        }
        cout << "  Please enter a whole number between " << lo << " and " << hi << ".\n";
    }
}

static string toBinary(unsigned v, int n) {
    string s;
    for (int i = n - 1; i >= 0; --i) s += ((v >> i) & 1u) ? '1' : '0';
    return s;
}

static bool validIdentifier(const string& s) {
    if (s.empty() || !(isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char ch : s)
        if (!(isalnum(static_cast<unsigned char>(ch)) || ch == '_')) return false;
    return true;
}

static const vector<string> GATE_NAMES = {"AND", "OR", "NOT", "XOR", "NAND", "NOR", "XNOR", "BUF"};

static string askGateType() {
    cout << "\nAvailable gates:\n";
    for (size_t i = 0; i < GATE_NAMES.size(); ++i) cout << "  " << (i + 1) << ". " << GATE_NAMES[i] << "\n";
    while (true) {
        string s = upperCase(ask("Choose a gate (number or name): "));
        for (size_t i = 0; i < GATE_NAMES.size(); ++i)
            if (s == GATE_NAMES[i] || s == to_string(i + 1)) return GATE_NAMES[i];
        cout << "  Not a valid gate. Try again.\n";
    }
}

// A single gate wired to fresh inputs A (and B for two-input gates).
static unique_ptr<Circuit> buildSingleGate(const string& type) {
    auto c = make_unique<Circuit>(type + " gate");
    Node* a = c->addInput("A");
    bool binary = makeGate(type)->inputs.size() == 2;
    Node* g = binary ? c->addGate(type, {a, c->addInput("B")}, "G") : c->addGate(type, {a}, "G");
    c->markOutput("OUT", g);
    return c;
}

static void listOperations() {
    cout << R"(
==================== SUPPORTED OPERATIONS ====================

LOGIC GATES (the building blocks)
  AND   OR   NOT   XOR   NAND   NOR   XNOR   BUF

WHAT YOU CAN DO WITH THEM
  1  BIT CALCULATOR: enter your own bits (up to 16), choose an operation,
     and get the answer computed by a gate-level circuit:
        Logic, bit by bit (two bit-strings):  AND  OR  XOR  NAND  NOR  XNOR
        Logic, bit by bit (one bit-string):   NOT  BUF
        Arithmetic (two bit-strings):         ADD  SUB
  3  Truth table of any single gate
  4  Build your own circuit (name inputs, wire gates, pick outputs),
     then print its truth table, evaluate it, or view its evaluation order
  5  Half adder   : adds two 1-bit numbers      -> SUM, CARRY
  6  Full adder   : adds two bits + carry-in    -> SUM, COUT
  7  N-bit ripple-carry adder (1 to 8 bits)     -> you enter two numbers
  8  N-bit ALU (1 to 8 bits), operations:
        0  ADD   A + B
        1  SUB   A - B   (two's complement; carry = 1 means no borrow)
        2  AND   bitwise A AND B
        3  XOR   bitwise A XOR B
     Flags: COUT (carry, arithmetic ops only) and ZERO (result is 0)
  9  Full demo of everything above
  10 Self-verification (every adder/ALU input combination is checked
     against native C++ arithmetic)


==============================================================
)";
}

static void printMenu() {
    cout << R"(
=========== DIGITAL LOGIC CIRCUIT SIMULATOR ===========
  1. Calculate with my own bits (I choose the operation)
  2. List supported operations
  3. Truth table of a single gate
  4. Build my own custom circuit
  5. Half adder
  6. Full adder
  7. N-bit adder (add two numbers with gates)
  8. N-bit ALU (ADD / SUB / AND / XOR)
  9. Run full demo
 10. Run self-verification
  0. Exit
=======================================================
)";
}

/* ---------------------------------------------------------------------------
 Bit calculator: user supplies bit-strings and picks the operation
 ---------------------------------------------------------------------------*/
static bool isBitString(const string& s) {
    if (s.empty()) return false;
    for (char ch : s)
        if (ch != '0' && ch != '1') return false;
    return true;
}

static string askBits(const string& prompt) {
    while (true) {
        string s = ask(prompt);
        if (isBitString(s) && s.size() <= 16) return s;
        cout << "  Enter 1 to 16 bits using only 0 and 1 (example: 1011).\n";
    }
}

static unsigned bitsToValue(const string& s) {  // MSB-first string -> number
    unsigned v = 0;
    for (char ch : s) v = (v << 1) | (ch == '1' ? 1u : 0u);
    return v;
}

static string padRight(string s, size_t width) {
    if (s.size() < width) s += string(width - s.size(), ' ');
    return s;
}

struct CalcOp {
    const char* name;
    const char* help;
    bool needsB;  // true if the operation takes two bit-strings
};

static const vector<CalcOp> CALC_OPS = {
    {"AND", "1 only where both bits are 1", true},
    {"OR", "1 where at least one bit is 1", true},
    {"XOR", "1 where the bits differ", true},
    {"NAND", "opposite of AND", true},
    {"NOR", "opposite of OR", true},
    {"XNOR", "1 where the bits are equal", true},
    {"NOT", "flips every bit (one bit-string)", false},
    {"BUF", "copies every bit (one bit-string)", false},
    {"ADD", "binary addition, A + B", true},
    {"SUB", "binary subtraction, A - B", true},
};

static const CalcOp& askCalcOp() {
    cout << "\nOperations:\n";
    for (size_t i = 0; i < CALC_OPS.size(); ++i)
        cout << "  " << padRight(to_string(i + 1) + ".", 4) << padRight(CALC_OPS[i].name, 5) << "- "
             << CALC_OPS[i].help << "\n";
    while (true) {
        string s = upperCase(ask("Choose operation (number or name): "));
        for (size_t i = 0; i < CALC_OPS.size(); ++i)
            if (s == CALC_OPS[i].name || s == to_string(i + 1)) return CALC_OPS[i];
        cout << "  Not a valid operation. Try again.\n";
    }
}

// One gate per bit: result bit i = GATE(A_i, B_i).
static unique_ptr<Circuit> buildBitwise(const string& type, int n) {
    auto c = make_unique<Circuit>(to_string(n) + "-bit " + type);
    bool binary = makeGate(type)->inputs.size() == 2;
    vector<Node*> a, b;
    for (int i = 0; i < n; ++i) a.push_back(c->addInput("A" + to_string(i)));
    if (binary)
        for (int i = 0; i < n; ++i) b.push_back(c->addInput("B" + to_string(i)));
    for (int i = 0; i < n; ++i) {
        string k = to_string(i);
        Node* g = binary ? c->addGate(type, {a[i], b[i]}, "G" + k) : c->addGate(type, {a[i]}, "G" + k);
        c->markOutput("R" + k, g);
    }
    return c;
}

static void opCalculator() {
    cout << "\n--- Bit calculator ---\n"
            "  1) enter your bits   2) choose an operation   3) the answer is computed gate by gate\n";
    auto line = [](const string& label, const string& value) {
        cout << "  " << padRight(label, 9) << "= " << value << "\n";
    };

    while (true) {
        string a = askBits("\nFirst bit-string (e.g. 1011): ");
        const CalcOp& op = askCalcOp();
        string type = op.name;
        string b;
        if (op.needsB) {
            b = askBits("Second bit-string (e.g. 0110): ");
            if (a.size() != b.size()) {
                size_t w = max(a.size(), b.size());
                a = string(w - a.size(), '0') + a;
                b = string(w - b.size(), '0') + b;
                cout << "  (Shorter input padded with leading zeros to " << w << " bits.)\n";
            }
        }
        int n = static_cast<int>(a.size());
        unsigned x = bitsToValue(a);
        unsigned y = op.needsB ? bitsToValue(b) : 0;
        cout << "\n";

        if (type == "ADD") {
            auto adder = buildRippleAdder(n);
            auto r = runAdder(*adder, x, y, 0, n);
            line("A", a);
            line("B", b);
            line("A + B", to_string(r.cout) + toBinary(r.sum, n));
            cout << "  Decimal : " << x << " + " << y << " = " << (x + y) << "  (carry out = " << r.cout << ")\n"
                 << "  Gate-level nodes used: " << adder->size() << "\n";
        } else if (type == "SUB") {
            auto alu = buildAlu(n);
            auto r = runAlu(*alu, x, y, ALU_SUB, n);
            line("A", a);
            line("B", b);
            line("A - B", toBinary(r.result, n));
            if (x >= y) {
                cout << "  Decimal : " << x << " - " << y << " = " << (x - y) << "  (no borrow)\n";
            } else {
                cout << "  Decimal : " << x << " - " << y << " = -" << (y - x) << "  (borrow: shown wrapped as "
                     << r.result << " in " << n << " bits, two's complement)\n";
            }
            cout << "  Gate-level nodes used: " << alu->size() << "\n";
        } else {
            auto c = buildBitwise(type, n);
            Assignment asg;
            loadWord(asg, "A", x, n);
            if (op.needsB) loadWord(asg, "B", y, n);
            auto out = c->evaluate(asg);
            line("A", a);
            if (op.needsB) line("B", b);
            string label = op.needsB ? "A " + type + " B" : type + " A";
            line(label, toBinary(readWord(out, "R", n), n));
            cout << "  Gate-level nodes used: " << c->size() << " (one " << type << " gate per bit)\n";
        }

        string again = upperCase(ask("\nDo another calculation? (y/n): "));
        if (again.empty() || again[0] != 'Y') return;
    }
}

static void opGateTruthTable() {
    string type = askGateType();
    buildSingleGate(type)->printTruthTable();
}

static void customCircuitMenu(Circuit& c) {
    while (true) {
        cout << "\n--- Custom circuit: " << c.name() << " ---\n"
             << "  1. Print truth table\n  2. Evaluate with my own input values\n"
             << "  3. Show evaluation order (topological sort)\n  0. Back to main menu\n";
        int choice = askInt("Choose: ", 0, 3);
        if (choice == 0) return;
        if (choice == 1) {
            if (c.inputNames().size() > 10) cout << "  Too many inputs for a truth table (max 10).\n";
            else c.printTruthTable();
        } else if (choice == 2) {
            unordered_map<string, int> asg;
            for (const auto& name : c.inputNames()) asg[name] = askInt("  " + name + " (0 or 1): ", 0, 1);
            auto out = c.evaluate(asg);
            cout << "\n  Result:\n";
            for (const auto& label : c.outputLabels()) cout << "    " << label << " = " << out[label] << "\n";
        } else {
            c.describe();
        }
    }
}

static void opCustomCircuit() {
    cout << R"(
--- Custom circuit builder ---
  Step 1: name your inputs, e.g.    A B C
  Step 2: define gates, one per line:    name = GATE source1 [source2]
            e.g.   g1 = AND A B      g2 = NOT C      g3 = XOR g1 g2
          Sources must be inputs or gates you defined earlier.
          Type DONE when you have finished.
  Step 3: choose which nodes are the outputs.
  (Names are case-sensitive. Gates: AND OR NOT XOR NAND NOR XNOR BUF)
)";
    Circuit c("My custom circuit");

    // Step 1: inputs
    while (true) {
        auto names = splitWords(ask("\nInput names: "));
        bool ok = !names.empty();
        for (const auto& n : names) ok = ok && validIdentifier(n);
        if (!ok) {
            cout << "  Use letters, digits and underscores (not starting with a digit).\n";
            continue;
        }
        if (unordered_set<string>(names.begin(), names.end()).size() != names.size()) {
            cout << "  Input names must be unique.\n";
            continue;
        }
        for (const auto& n : names) c.addInput(n);
        break;
    }

    // Step 2: gates
    int gateCount = 0;
    while (true) {
        string line = ask("gate> ");
        if (upperCase(line) == "DONE") {
            if (gateCount == 0) {
                cout << "  Add at least one gate first.\n";
                continue;
            }
            break;
        }
        for (char& ch : line)
            if (ch == '=') ch = ' ';
        auto w = splitWords(line);
        if (w.size() < 3 || !validIdentifier(w[0])) {
            cout << "  Format: name = GATE source1 [source2]\n";
            continue;
        }
        vector<Node*> sources;
        bool ok = true;
        for (size_t i = 2; i < w.size(); ++i) {
            Node* s = c.find(w[i]);
            if (!s) {
                cout << "  Unknown source '" << w[i] << "' (define it first).\n";
                ok = false;
                break;
            }
            sources.push_back(s);
        }
        if (!ok) continue;
        try {
            c.addGate(w[1], sources, w[0]);
            ++gateCount;
            cout << "  added " << w[0] << " = " << upperCase(w[1]) << "(";
            for (size_t i = 2; i < w.size(); ++i) cout << w[i] << (i + 1 < w.size() ? ", " : "");
            cout << ")\n";
        } catch (const CircuitError& e) {
            cout << "  " << e.what() << "\n";
        }
    }

    // Step 3: outputs
    while (true) {
        auto names = splitWords(ask("\nOutput node names (e.g. g3): "));
        bool ok = !names.empty();
        for (const auto& n : names)
            if (!c.find(n)) {
                cout << "  Unknown node '" << n << "'.\n";
                ok = false;
                break;
            }
        if (!ok) continue;
        for (const auto& n : names) c.markOutput(n, c.find(n));
        break;
    }

    c.describe();
    customCircuitMenu(c);
}

static void opHalfAdder() {
    auto c = buildHalfAdder();
    unordered_map<string, int> asg;
    asg["A"] = askInt("Bit A (0 or 1): ", 0, 1);
    asg["B"] = askInt("Bit B (0 or 1): ", 0, 1);
    auto out = c->evaluate(asg);
    cout << "\n  " << asg["A"] << " + " << asg["B"] << " = SUM " << out["SUM"] << ", CARRY " << out["CARRY"] << "\n";
    c->printTruthTable();
}

static void opFullAdder() {
    auto c = buildFullAdder();
    unordered_map<string, int> asg;
    asg["A"] = askInt("Bit A (0 or 1): ", 0, 1);
    asg["B"] = askInt("Bit B (0 or 1): ", 0, 1);
    asg["CIN"] = askInt("Carry-in (0 or 1): ", 0, 1);
    auto out = c->evaluate(asg);
    cout << "\n  " << asg["A"] << " + " << asg["B"] << " + " << asg["CIN"] << " = SUM " << out["SUM"]
         << ", COUT " << out["COUT"] << "\n";
    c->printTruthTable();
}

static void opAdder() {
    int n = askInt("Word size in bits (1-8): ", 1, 8);
    int maxVal = (1 << n) - 1;
    unsigned x = static_cast<unsigned>(askInt("First number  (0-" + to_string(maxVal) + "): ", 0, maxVal));
    unsigned y = static_cast<unsigned>(askInt("Second number (0-" + to_string(maxVal) + "): ", 0, maxVal));
    int cin_ = askInt("Carry-in (0 or 1): ", 0, 1);
    auto adder = buildRippleAdder(n);
    auto r = runAdder(*adder, x, y, cin_, n);
    cout << "\n  " << x << " + " << y << " + " << cin_ << " = " << r.sum << "  (carry out = " << r.cout << ")\n"
         << "  " << toBinary(x, n) << " + " << toBinary(y, n) << " + " << cin_ << " = " << r.cout
         << toBinary(r.sum, n) << "\n"
         << "  Built from " << adder->size() << " gate-level nodes.\n";
    if (r.cout) cout << "  Note: the true sum needs " << (n + 1) << " bits; the carry-out is the extra bit.\n";
}

static void opAlu() {
    int n = askInt("Word size in bits (1-8): ", 1, 8);
    int maxVal = (1 << n) - 1;
    unsigned x = static_cast<unsigned>(askInt("A (0-" + to_string(maxVal) + "): ", 0, maxVal));
    unsigned y = static_cast<unsigned>(askInt("B (0-" + to_string(maxVal) + "): ", 0, maxVal));
    cout << "\nOperations:\n  0. ADD\n  1. SUB\n  2. AND\n  3. XOR\n";
    int op = askInt("Choose operation (0-3): ", 0, 3);
    auto alu = buildAlu(n);
    auto r = runAlu(*alu, x, y, op, n);
    cout << "\n  " << aluOpName(op) << "(" << x << ", " << y << ") = " << r.result << "   [" << toBinary(r.result, n)
         << "]\n  Flags: carry = " << r.carry << ", zero = " << r.zero << "\n";
    if (op == ALU_SUB) cout << "  (For SUB, carry = 1 means no borrow, i.e. A >= B.)\n";
    cout << "  Built from " << alu->size() << " gate-level nodes.\n";
}

static void interactiveMenu() {
    cout << "Digital Logic Circuit Simulator - INTERACTIVE version\n"
         << "Welcome! Choose an option by number.\n";
    listOperations();  // show everything the program can do, once, at startup
    while (true) {
        printMenu();
        int choice = askInt("Your choice: ", 0, 10);
        if (choice == 0) {
            cout << "Goodbye!\n";
            return;
        }
        try {
            switch (choice) {
                case 1: opCalculator(); break;
                case 2: listOperations(); break;
                case 3: opGateTruthTable(); break;
                case 4: opCustomCircuit(); break;
                case 5: opHalfAdder(); break;
                case 6: opFullAdder(); break;
                case 7: opAdder(); break;
                case 8: opAlu(); break;
                case 9: demo(); break;
                case 10: verifyAll(); break;
            }
        } catch (const CircuitError& e) {
            cout << "  Circuit error: " << e.what() << "\n";
        }
    }
}

// Keeps the console window open when the program is started 
static void waitBeforeExit() {
    cout << "\nPress Enter to close...";
    cout.flush();
    string dummy;
    getline(cin, dummy);
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && string(argv[1]) == "--demo") demo();
        else interactiveMenu();
    } catch (const InputClosed&) {
        cout << "\n(No keyboard input is available. Run this program from a terminal "
                "such as the MSYS2 UCRT64 terminal, PowerShell or cmd, not from an output-only panel.)\n";
        return 0;
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        waitBeforeExit();
        return 1;
    }
    waitBeforeExit();
    return 0;
}
