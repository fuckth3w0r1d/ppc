#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <sstream>
#include <vector>
#include "logger.h"

[[noreturn]] void die(const std::string& msg)
{
    logger::error(msg);
    std::exit(EXIT_FAILURE);
}

// ============================= 获得后缀正则token流
enum RegexTokenType
{
    RE_LITERAL,      // 普通字符
    RE_CHAR_CLASS,   // 字符类，如 [a-zA-Z_]
    RE_OR,           // |
    RE_STAR,         // *
    RE_PLUS,         // +
    RE_OPTIONAL,     // ?
    RE_LPAREN,       // (
    RE_RPAREN,       // )
    RE_CONCAT        // 连接符，内部使用
};

struct RegexToken
{
    RegexTokenType type;
    char ch;
    std::vector<char> chars;

    RegexToken(RegexTokenType t, char v) : type(t), ch(v) {}
    RegexToken(const std::vector<char>& s) : type(RE_CHAR_CLASS), ch('\0'), chars(s) {}
};

bool is_space(char c)
{
    return (c == ' '  ||
            c == '\t' ||
            c == '\n' ||
            c == '\r' ||
            c == '\f' ||
            c == '\v');
}

bool is_atom_end(const RegexToken& t)
{
    return t.type == RE_LITERAL   ||
           t.type == RE_CHAR_CLASS ||
           t.type == RE_RPAREN     ||
           t.type == RE_OPTIONAL   ||
           t.type == RE_PLUS       ||
           t.type == RE_STAR;
}

bool is_atom_begin(const RegexToken& t)
{
    return t.type == RE_LITERAL ||
           t.type == RE_CHAR_CLASS ||
           t.type == RE_LPAREN;
}

bool is_regex_operand(const RegexToken& t)
{
    return t.type == RE_LITERAL || t.type == RE_CHAR_CLASS;
}

bool is_regex_operator(const RegexToken& t)
{
    return t.type == RE_OR     ||
           t.type == RE_CONCAT ||
           t.type == RE_STAR   ||
           t.type == RE_PLUS   ||
           t.type == RE_OPTIONAL;
}

// 判断两个正则 token 之间是否需要连接符
bool need_concat(const RegexToken& a, const RegexToken& b)
{
    return is_atom_end(a) && is_atom_begin(b);
}

std::vector<char> parse_char_class(const std::string& re, size_t& i)
{
    // 当前 re[i] == '['
    i++;
    if(i >= re.size())
    {
        die("unterminated char class");
    }

    std::set<char> chars;
    while(i < re.size() && re[i] != ']')
    {
        if(i + 2 < re.size() && re[i + 1] == '-' && re[i + 2] != ']')
        {
            char first = re[i];
            char last = re[i + 2];
            if(first > last)
            {
                die("invalid range in char class");
            }
            for(char c = first; c <= last; ++c)
            {
                chars.insert(c);
            }
            i += 3;
        }
        else
        {
            chars.insert(re[i]);
            i++;
        }
    }

    if(i >= re.size() || re[i] != ']')
    {
        die("unterminated char class");
    }

    if(chars.empty())
    {
        die("empty char class");
    }

    std::vector<char> result(chars.begin(), chars.end());
    return result;
}

std::vector<RegexToken> tokenize_regex(const std::string& re)
{
    std::vector<RegexToken> output;
    for(size_t i = 0; i < re.size(); ++i)
    {
        char ch = re[i];
        if(is_space(ch))
        {
            continue;
        }
        if(ch == '[')
        {
            std::vector<char> cls = parse_char_class(re, i);
            output.emplace_back(cls);
            continue;
        }

        switch(ch)
        {
            case '|': output.emplace_back(RE_OR, ch); break;
            case '(': output.emplace_back(RE_LPAREN, ch); break;
            case ')': output.emplace_back(RE_RPAREN, ch); break;
            case '*': output.emplace_back(RE_STAR, ch); break;
            case '?': output.emplace_back(RE_OPTIONAL, ch); break;
            case '+': output.emplace_back(RE_PLUS, ch); break;
            default: output.emplace_back(RE_LITERAL, ch); break;
        }
    }

    return output;
}

std::vector<RegexToken> insert_concat(const std::vector<RegexToken>& re)
{
    if(re.empty()) return {};

    std::vector<RegexToken> output;
    output.emplace_back(re[0]);
    for(size_t i = 1; i < re.size(); ++i)
    {
        if(need_concat(re[i - 1], re[i]))
        {
            output.emplace_back(RE_CONCAT, '\0');
        }
        output.emplace_back(re[i]);
    }

    return output;
}

int regex_precedence(const RegexToken& t)
{
    switch(t.type)
    {
        case RE_OR:
            return 1;
        case RE_CONCAT:
            return 2;
        case RE_STAR:
        case RE_PLUS:
        case RE_OPTIONAL:
            return 3;
        default:
            return 0;
    }
}

std::vector<RegexToken> infix_to_postfix(const std::vector<RegexToken>& infix)
{
    std::vector<RegexToken> output;
    std::stack<RegexToken> ops;
    for(const auto& token : infix)
    {
        if(is_regex_operand(token))
        {
            output.emplace_back(token);
        }
        else if(token.type == RE_LPAREN)
        {
            ops.push(token);
        }
        else if(token.type == RE_RPAREN)
        {
            while(!ops.empty() && ops.top().type != RE_LPAREN)
            {
                output.emplace_back(ops.top());
                ops.pop();
            }
            if(ops.empty())
            {
                die("mismatched parentheses");
            }

            ops.pop();
        }
        else if(is_regex_operator(token))
        {
            while(!ops.empty() &&
                  ops.top().type != RE_LPAREN &&
                  regex_precedence(ops.top()) >= regex_precedence(token))
            {
                output.emplace_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        }
        else
        {
            die("invalid token in infix_to_postfix");
        }
    }
    while(!ops.empty())
    {
        if(ops.top().type == RE_LPAREN || ops.top().type == RE_RPAREN)
        {
            die("mismatched parentheses");
        }
        output.emplace_back(ops.top());
        ops.pop();
    }
    return output;
}


// ==================================== token 类型

enum TokenKind
{
    TK_NONE = 0,
    TK_SKIP,

    TK_KW_INT,
    TK_KW_RETURN,
    TK_KW_IF,
    TK_KW_ELSE,
    TK_KW_WHILE,
    TK_KW_FOR,
    TK_KW_VOID,
    TK_KW_CHAR,
    TK_KW_FLOAT,
    TK_KW_DOUBLE,

    TK_IDENT,
    TK_NUMBER,
    TK_FLOAT,
    TK_STRING,
    TK_CHAR_LITERAL,

    TK_EQ,
    TK_NE,
    TK_LE,
    TK_GE,
    TK_AND,
    TK_OR,

    TK_ASSIGN,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_PERCENT,
    TK_LT,
    TK_GT,
    TK_NOT,

    TK_SEMI,
    TK_COMMA,
    TK_DOT,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACE,
    TK_RBRACE,
    TK_LBRACKET,
    TK_RBRACKET
};

std::string token_kind_name(int kind)
{
    switch(kind)
    {
        case TK_SKIP: return "SKIP";
        case TK_KW_INT: return "KW_INT";
        case TK_KW_RETURN: return "KW_RETURN";
        case TK_KW_IF: return "KW_IF";
        case TK_KW_ELSE: return "KW_ELSE";
        case TK_KW_WHILE: return "KW_WHILE";
        case TK_KW_FOR: return "KW_FOR";
        case TK_KW_VOID: return "KW_VOID";
        case TK_KW_CHAR: return "KW_CHAR";
        case TK_KW_FLOAT: return "KW_FLOAT";
        case TK_KW_DOUBLE: return "KW_DOUBLE";
        case TK_IDENT: return "IDENT";
        case TK_NUMBER: return "NUMBER";
        case TK_FLOAT: return "FLOAT";
        case TK_STRING: return "STRING";
        case TK_CHAR_LITERAL: return "CHAR";
        case TK_EQ: return "EQ";
        case TK_NE: return "NE";
        case TK_LE: return "LE";
        case TK_GE: return "GE";
        case TK_AND: return "AND";
        case TK_OR: return "OR";
        case TK_ASSIGN: return "ASSIGN";
        case TK_PLUS: return "PLUS";
        case TK_MINUS: return "MINUS";
        case TK_STAR: return "STAR";
        case TK_SLASH: return "SLASH";
        case TK_PERCENT: return "PERCENT";
        case TK_LT: return "LT";
        case TK_GT: return "GT";
        case TK_NOT: return "NOT";
        case TK_SEMI: return "SEMI";
        case TK_COMMA: return "COMMA";
        case TK_DOT: return "DOT";
        case TK_LPAREN: return "LPAREN";
        case TK_RPAREN: return "RPAREN";
        case TK_LBRACE: return "LBRACE";
        case TK_RBRACE: return "RBRACE";
        case TK_LBRACKET: return "LBRACKET";
        case TK_RBRACKET: return "RBRACKET";
        default: return "UNKNOWN";
    }
}

// ==================================== 后缀正则转 NFA

static constexpr int EPS = -1;
static constexpr int ASCII_COUNT = 128;

struct NFAEdge
{
    int to;
    int symbol;   // 普通字符或 EPS
};

struct NFAState
{
    std::vector<NFAEdge> edges;
    int token_kind = TK_NONE;
    int rule_priority = std::numeric_limits<int>::max();
};

struct NFA
{
    std::vector<NFAState> states;
    int start = -1;
    int accept = -1;
};

struct NFAFragment
{
    int start;
    int accept;
};

int new_nfa_state(NFA& nfa)
{
    nfa.states.emplace_back();
    return nfa.states.size() - 1;
}

void add_nfa_edge(NFA& nfa, int from, int to, int symbol)
{
    nfa.states[from].edges.push_back({to, symbol});
}

NFAFragment pop_nfa_frag(std::stack<NFAFragment>& st, const std::string& error)
{
    if(st.empty())
    {
        die(error);
    }

    NFAFragment frag = st.top();
    st.pop();
    return frag;
}

NFAFragment symbol_frag(NFA& nfa, int symbol)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    add_nfa_edge(nfa, start, accept, symbol);
    return {start, accept};
}

NFAFragment char_class_frag(NFA& nfa, const std::vector<char>& chars)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    for(char ch : chars)
    {
        add_nfa_edge(nfa, start, accept, ch);
    }
    return {start, accept};
}

NFAFragment concat_frag(NFA& nfa, NFAFragment lhs, NFAFragment rhs)
{
    add_nfa_edge(nfa, lhs.accept, rhs.start, EPS);
    return {lhs.start, rhs.accept};
}

NFAFragment or_frag(NFA& nfa, NFAFragment lhs, NFAFragment rhs)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    add_nfa_edge(nfa, start, lhs.start, EPS);
    add_nfa_edge(nfa, start, rhs.start, EPS);
    add_nfa_edge(nfa, lhs.accept, accept, EPS);
    add_nfa_edge(nfa, rhs.accept, accept, EPS);
    return {start, accept};
}

NFAFragment star_frag(NFA& nfa, NFAFragment sub)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    add_nfa_edge(nfa, start, sub.start, EPS);
    add_nfa_edge(nfa, start, accept, EPS);
    add_nfa_edge(nfa, sub.accept, sub.start, EPS);
    add_nfa_edge(nfa, sub.accept, accept, EPS);
    return {start, accept};
}

NFAFragment plus_frag(NFA& nfa, NFAFragment sub)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    add_nfa_edge(nfa, start, sub.start, EPS);
    add_nfa_edge(nfa, sub.accept, sub.start, EPS);
    add_nfa_edge(nfa, sub.accept, accept, EPS);
    return {start, accept};
}

NFAFragment optional_frag(NFA& nfa, NFAFragment sub)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    add_nfa_edge(nfa, start, sub.start, EPS);
    add_nfa_edge(nfa, start, accept, EPS);
    add_nfa_edge(nfa, sub.accept, accept, EPS);
    return {start, accept};
}

int append_nfa(NFA& dst, const NFA& src)
{
    int offset = dst.states.size();

    for(const auto& st : src.states)
    {
        dst.states.push_back(st);
    }

    for(size_t i = offset; i < dst.states.size(); ++i)
    {
        for(auto& edge : dst.states[i].edges)
        {
            edge.to += offset;
        }
    }

    return offset;
}

NFA build_nfa_from_postfix(const std::vector<RegexToken>& postfix)
{
    NFA nfa;
    std::stack<NFAFragment> st;

    for(const auto& token : postfix)
    {
        switch(token.type)
        {
            case RE_LITERAL:
                st.push(symbol_frag(nfa, token.ch));
                break;
            case RE_CHAR_CLASS:
                st.push(char_class_frag(nfa, token.chars));
                break;
            case RE_CONCAT:
            {
                NFAFragment rhs = pop_nfa_frag(st, "invalid regex: concat needs two operands");
                NFAFragment lhs = pop_nfa_frag(st, "invalid regex: concat needs two operands");
                st.push(concat_frag(nfa, lhs, rhs));
                break;
            }
            case RE_OR:
            {
                NFAFragment rhs = pop_nfa_frag(st, "invalid regex: or needs two operands");
                NFAFragment lhs = pop_nfa_frag(st, "invalid regex: or needs two operands");
                st.push(or_frag(nfa, lhs, rhs));
                break;
            }
            case RE_STAR:
                st.push(star_frag(nfa, pop_nfa_frag(st, "invalid regex: star needs one operand")));
                break;
            case RE_PLUS:
                st.push(plus_frag(nfa, pop_nfa_frag(st, "invalid regex: plus needs one operand")));
                break;
            case RE_OPTIONAL:
                st.push(optional_frag(nfa, pop_nfa_frag(st, "invalid regex: ? needs one operand")));
                break;
            default:
                die("invalid token in postfix NFA construction");
        }
    }

    if(st.size() != 1)
    {
        die("invalid regex: malformed postfix expression");
    }
    NFAFragment res = st.top();
    nfa.start = res.start;
    nfa.accept = res.accept;
    return nfa;
}

NFA regex_to_nfa(const std::string& re)
{
    std::vector<RegexToken> tokens = tokenize_regex(re);
    std::vector<RegexToken> infix = insert_concat(tokens);
    std::vector<RegexToken> postfix = infix_to_postfix(infix);
    return build_nfa_from_postfix(postfix);
}

// ==================================== 词法规则 NFA

NFA token_nfa(int token_kind, int rule_priority, NFA nfa)
{
    nfa.states[nfa.accept].token_kind = token_kind;
    nfa.states[nfa.accept].rule_priority = rule_priority;
    return nfa;
}

NFA literal_to_nfa(const std::string& text)
{
    NFA nfa;
    nfa.start = new_nfa_state(nfa);
    int cur = nfa.start;
    for(char ch : text)
    {
        int next = new_nfa_state(nfa);
        add_nfa_edge(nfa, cur, next, static_cast<unsigned char>(ch));
        cur = next;
    }
    nfa.accept = cur;
    return nfa;
}

NFA one_or_more_chars_nfa(const std::vector<char>& chars)
{
    NFA nfa;
    int s = new_nfa_state(nfa);
    int t = new_nfa_state(nfa);
    for(char ch : chars)
    {
        add_nfa_edge(nfa, s, t, static_cast<unsigned char>(ch));
        add_nfa_edge(nfa, t, t, static_cast<unsigned char>(ch));
    }
    nfa.start = s;
    nfa.accept = t;
    return nfa;
}

template<typename Pred>
void add_ascii_edges_if(NFA& nfa, int from, int to, Pred pred)
{
    for(int ch = 0; ch < ASCII_COUNT; ++ch)
    {
        if(pred(ch))
        {
            add_nfa_edge(nfa, from, to, ch);
        }
    }
}

bool is_normal_string_char(int ch)
{
    return ch >= 0 && ch < ASCII_COUNT &&
           ch != '"' &&
           ch != '\\' &&
           ch != '\n' &&
           ch != '\r';
}

bool is_normal_char_literal_char(int ch)
{
    return ch >= 0 && ch < ASCII_COUNT &&
           ch != '\'' &&
           ch != '\\' &&
           ch != '\n' &&
           ch != '\r';
}

bool is_not_newline(int ch)
{
    return ch >= 0 && ch < ASCII_COUNT &&
           ch != '\n' &&
           ch != '\r';
}

NFA line_comment_to_nfa()
{
    NFA nfa;
    int s = new_nfa_state(nfa);
    int slash = new_nfa_state(nfa);
    int body = new_nfa_state(nfa);

    add_nfa_edge(nfa, s, slash, '/');
    add_nfa_edge(nfa, slash, body, '/');
    add_ascii_edges_if(nfa, body, body, is_not_newline);

    nfa.start = s;
    nfa.accept = body;
    return nfa;
}

NFA block_comment_to_nfa()
{
    NFA nfa;
    int s = new_nfa_state(nfa);
    int slash = new_nfa_state(nfa);
    int body = new_nfa_state(nfa);
    int star = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);

    add_nfa_edge(nfa, s, slash, '/');
    add_nfa_edge(nfa, slash, body, '*');

    for(int ch = 0; ch < ASCII_COUNT; ++ch)
    {
        if(ch == '*')
        {
            add_nfa_edge(nfa, body, star, ch);
            add_nfa_edge(nfa, star, star, ch);
        }
        else
        {
            add_nfa_edge(nfa, body, body, ch);
        }

        if(ch == '/')
        {
            add_nfa_edge(nfa, star, accept, ch);
        }
        else if(ch != '*')
        {
            add_nfa_edge(nfa, star, body, ch);
        }
    }

    nfa.start = s;
    nfa.accept = accept;
    return nfa;
}

NFA string_literal_to_nfa()
{
    NFA nfa;
    int s = new_nfa_state(nfa);
    int body = new_nfa_state(nfa);
    int esc = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);

    add_nfa_edge(nfa, s, body, '"');
    add_ascii_edges_if(nfa, body, body, is_normal_string_char);
    add_ascii_edges_if(nfa, esc, body, is_not_newline);
    add_nfa_edge(nfa, body, esc, '\\');
    add_nfa_edge(nfa, body, accept, '"');

    nfa.start = s;
    nfa.accept = accept;
    return nfa;
}

NFA char_literal_to_nfa()
{
    NFA nfa;
    int s = new_nfa_state(nfa);
    int body = new_nfa_state(nfa);
    int one_char = new_nfa_state(nfa);
    int esc = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);

    add_nfa_edge(nfa, s, body, '\'');
    add_ascii_edges_if(nfa, body, one_char, is_normal_char_literal_char);
    add_ascii_edges_if(nfa, esc, one_char, is_not_newline);
    add_nfa_edge(nfa, body, esc, '\\');
    add_nfa_edge(nfa, one_char, accept, '\'');

    nfa.start = s;
    nfa.accept = accept;
    return nfa;
}

NFA float_literal_to_nfa()
{
    NFA nfa;
    int s = new_nfa_state(nfa);
    int int_part = new_nfa_state(nfa);
    int dot_first = new_nfa_state(nfa);
    int frac = new_nfa_state(nfa);

    add_nfa_edge(nfa, s, dot_first, '.');
    add_nfa_edge(nfa, int_part, frac, '.');

    for(char ch = '0'; ch <= '9'; ++ch)
    {
        add_nfa_edge(nfa, s, int_part, ch);
        add_nfa_edge(nfa, int_part, int_part, ch);
        add_nfa_edge(nfa, dot_first, frac, ch);
        add_nfa_edge(nfa, frac, frac, ch);
    }

    nfa.start = s;
    nfa.accept = frac;
    return nfa;
}

NFA combine_rules_to_nfa(const std::vector<NFA>& rules)
{
    NFA combined;
    combined.start = new_nfa_state(combined);

    for(const auto& rule : rules)
    {
        int offset = append_nfa(combined, rule);
        add_nfa_edge(combined, combined.start, rule.start + offset, EPS);
    }

    return combined;
}

// ==================================== NFA 子集构造为 DFA

struct DFAState
{
    std::vector<int> next;
    int token_kind = TK_NONE;

    DFAState() : next(ASCII_COUNT, -1) {}
};

struct DFA
{
    std::vector<DFAState> states;
    int start = -1;
};

using StateSet = std::set<int>;

StateSet epsilon_closure(const NFA& nfa, const StateSet& input)
{
    StateSet result = input;
    std::stack<int> st;
    for(int s : input)
    {
        st.push(s);
    }

    while(!st.empty())
    {
        int cur = st.top();
        st.pop();
        for(const auto& e : nfa.states[cur].edges)
        {
            if(e.symbol == EPS && result.count(e.to) == 0)
            {
                result.insert(e.to);
                st.push(e.to);
            }
        }
    }
    return result;
}

StateSet move_set(const NFA& nfa, const StateSet& input, int symbol)
{
    StateSet result;
    for(int s : input)
    {
        for(const auto& e : nfa.states[s].edges)
        {
            if(e.symbol == symbol)
            {
                result.insert(e.to);
            }
        }
    }
    return result;
}

int choose_token_kind(const NFA& nfa, const StateSet& nfa_states)
{
    int best_priority = std::numeric_limits<int>::max();
    int token_kind = TK_NONE;

    for(int s : nfa_states)
    {
        const NFAState& nstate = nfa.states[s];
        if(nstate.token_kind != TK_NONE && nstate.rule_priority < best_priority)
        {
            token_kind = nstate.token_kind;
            best_priority = nstate.rule_priority;
        }
    }

    return token_kind;
}

int add_dfa_state(DFA& dfa, const NFA& nfa, const StateSet& nfa_states)
{
    DFAState dstate;
    dstate.token_kind = choose_token_kind(nfa, nfa_states);
    dfa.states.push_back(dstate);
    return dfa.states.size() - 1;
}

DFA nfa_to_dfa(const NFA& nfa)
{
    DFA dfa;
    std::map<StateSet, int> ids;
    std::queue<StateSet> work;

    StateSet start_input;
    start_input.insert(nfa.start);
    StateSet start_set = epsilon_closure(nfa, start_input);

    dfa.start = add_dfa_state(dfa, nfa, start_set);
    ids[start_set] = dfa.start;
    work.push(start_set);

    while(!work.empty())
    {
        StateSet cur_set = work.front();
        work.pop();
        int cur_id = ids[cur_set];

        for(int symbol = 0; symbol < ASCII_COUNT; ++symbol)
        {
            StateSet moved = move_set(nfa, cur_set, symbol);
            if(moved.empty())
            {
                continue;
            }

            StateSet next_set = epsilon_closure(nfa, moved);
            if(ids.count(next_set) == 0)
            {
                int next_id = add_dfa_state(dfa, nfa, next_set);
                ids[next_set] = next_id;
                work.push(next_set);
            }
            dfa.states[cur_id].next[symbol] = ids[next_set];
        }
    }

    return dfa;
}

// ==================================== DFA 最小化

std::vector<int> build_minimize_signature(const DFA& dfa,
                                          const std::vector<int>& partition,
                                          int state)
{
    std::vector<int> sig;
    sig.push_back(dfa.states[state].token_kind);
    for(int symbol = 0; symbol < ASCII_COUNT; ++symbol)
    {
        int to = dfa.states[state].next[symbol];
        sig.push_back(to == -1 ? -1 : partition[to]);
    }
    return sig;
}

DFA minimize_dfa(const DFA& dfa)
{
    int n = dfa.states.size();
    std::vector<int> partition(n, 0);
    std::map<int, int> initial_class_by_token;
    int class_count = 0;

    for(int i = 0; i < n; ++i)
    {
        int token = dfa.states[i].token_kind;
        if(initial_class_by_token.count(token) == 0)
        {
            initial_class_by_token[token] = class_count++;
        }
        partition[i] = initial_class_by_token[token];
    }

    bool changed = true;
    while(changed)
    {
        changed = false;
        std::map<std::vector<int>, int> sig_class;
        std::vector<int> next_partition(n, 0);
        int next_count = 0;

        for(int i = 0; i < n; ++i)
        {
            std::vector<int> sig = build_minimize_signature(dfa, partition, i);
            if(sig_class.count(sig) == 0)
            {
                sig_class[sig] = next_count++;
            }
            next_partition[i] = sig_class[sig];
        }

        if(next_partition != partition)
        {
            changed = true;
            partition = next_partition;
            class_count = next_count;
        }
    }

    DFA min_dfa;
    min_dfa.states.resize(class_count);
    min_dfa.start = partition[dfa.start];

    std::vector<bool> filled(class_count, false);
    for(int i = 0; i < n; ++i)
    {
        int c = partition[i];
        if(filled[c])
        {
            continue;
        }

        filled[c] = true;
        min_dfa.states[c].token_kind = dfa.states[i].token_kind;
        for(int symbol = 0; symbol < ASCII_COUNT; ++symbol)
        {
            int to = dfa.states[i].next[symbol];
            min_dfa.states[c].next[symbol] = to == -1 ? -1 : partition[to];
        }
    }

    return min_dfa;
}

// ==================================== 最小 C 词法规则和扫描器

std::vector<NFA> build_min_c_rules()
{
    std::vector<NFA> rules;
    int p = 1;

    auto literal = [&](int kind, const std::string& text) {
        rules.push_back(token_nfa(kind, p++, literal_to_nfa(text)));
    };
    auto regex = [&](int kind, const std::string& re) {
        rules.push_back(token_nfa(kind, p++, regex_to_nfa(re)));
    };
    auto raw_nfa = [&](int kind, NFA nfa) {
        rules.push_back(token_nfa(kind, p++, nfa));
    };

    raw_nfa(TK_SKIP, one_or_more_chars_nfa({' ', '\t', '\n', '\r', '\f', '\v'}));
    raw_nfa(TK_SKIP, line_comment_to_nfa());
    raw_nfa(TK_SKIP, block_comment_to_nfa());

    literal(TK_KW_INT, "int");
    literal(TK_KW_RETURN, "return");
    literal(TK_KW_IF, "if");
    literal(TK_KW_ELSE, "else");
    literal(TK_KW_WHILE, "while");
    literal(TK_KW_FOR, "for");
    literal(TK_KW_VOID, "void");
    literal(TK_KW_CHAR, "char");
    literal(TK_KW_FLOAT, "float");
    literal(TK_KW_DOUBLE, "double");

    regex(TK_IDENT, "[a-zA-Z_]([a-zA-Z0-9_])*");
    raw_nfa(TK_FLOAT, float_literal_to_nfa());
    regex(TK_NUMBER, "[0-9]+");
    raw_nfa(TK_STRING, string_literal_to_nfa());
    raw_nfa(TK_CHAR_LITERAL, char_literal_to_nfa());

    literal(TK_EQ, "==");
    literal(TK_NE, "!=");
    literal(TK_LE, "<=");
    literal(TK_GE, ">=");
    literal(TK_AND, "&&");
    literal(TK_OR, "||");

    literal(TK_ASSIGN, "=");
    literal(TK_PLUS, "+");
    literal(TK_MINUS, "-");
    literal(TK_STAR, "*");
    literal(TK_SLASH, "/");
    literal(TK_PERCENT, "%");
    literal(TK_LT, "<");
    literal(TK_GT, ">");
    literal(TK_NOT, "!");

    literal(TK_SEMI, ";");
    literal(TK_COMMA, ",");
    literal(TK_DOT, ".");
    literal(TK_LPAREN, "(");
    literal(TK_RPAREN, ")");
    literal(TK_LBRACE, "{");
    literal(TK_RBRACE, "}");
    literal(TK_LBRACKET, "[");
    literal(TK_RBRACKET, "]");

    return rules;
}

struct Lexeme
{
    int token_kind;
    std::string text;
    int line;
    int col;
};

void advance_position(const std::string& text, int& line, int& col)
{
    for(char ch : text)
    {
        if(ch == '\n')
        {
            line++;
            col = 1;
        }
        else
        {
            col++;
        }
    }
}

std::vector<Lexeme> scan_source(const DFA& dfa, const std::string& source)
{
    std::vector<Lexeme> tokens;
    size_t pos = 0;
    int line = 1;
    int col = 1;

    while(pos < source.size())
    {
        int state = dfa.start;
        size_t i = pos;
        size_t last_accept_pos = pos;
        int last_accept_kind = TK_NONE;

        while(i < source.size())
        {
            unsigned char ch = static_cast<unsigned char>(source[i]);
            if(ch >= ASCII_COUNT)
            {
                break;
            }

            int next = dfa.states[state].next[ch];
            if(next == -1)
            {
                break;
            }

            state = next;
            i++;

            if(dfa.states[state].token_kind != TK_NONE)
            {
                last_accept_pos = i;
                last_accept_kind = dfa.states[state].token_kind;
            }
        }

        if(last_accept_kind == TK_NONE)
        {
            logger::error("lex error near line ", line);
            logger::error("column: ", col);
            logger::error("char: ", source[pos]);
            std::exit(EXIT_FAILURE);
        }

        std::string text = source.substr(pos, last_accept_pos - pos);
        if(last_accept_kind != TK_SKIP)
        {
            tokens.push_back({last_accept_kind, text, line, col});
        }

        advance_position(text, line, col);
        pos = last_accept_pos;
    }

    return tokens;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path);
    if(!in)
    {
        logger::error("cannot open input file: ", path);
        std::exit(EXIT_FAILURE);
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void print_tokens(std::ostream& os, const std::vector<Lexeme>& tokens)
{
    for(const auto& tok : tokens)
    {
        os << tok.line << ':' << tok.col
           << "  " << token_kind_name(tok.token_kind)
           << "  " << tok.text << '\n';
    }
}

int main(int argc, char** argv)
{
    if(argc != 3)
    {
        logger::error("usage: ./lexer <input.c> <output.tokens>");
        return EXIT_FAILURE;
    }

    std::string source = read_file(argv[1]);

    std::vector<NFA> rules = build_min_c_rules();
    NFA nfa = combine_rules_to_nfa(rules);
    DFA dfa = nfa_to_dfa(nfa);
    DFA min_dfa = minimize_dfa(dfa);

    std::vector<Lexeme> tokens = scan_source(min_dfa, source);

    std::ofstream out(argv[2]);
    if(!out)
    {
        logger::error("cannot open output file: ", argv[2]);
        return EXIT_FAILURE;
    }
    print_tokens(out, tokens);

    logger::info("NFA states: ", nfa.states.size());
    logger::info("DFA states: ", dfa.states.size());
    logger::info("min DFA states: ", min_dfa.states.size());
    logger::info("token file: ", argv[2]);

    return 0;
}
