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


// ============================= 获得后缀正则token流
enum ReTokenType
{
    LITERAL,      // 普通字符
    CHAR_CLASS,   // 字符类，如 [a-zA-Z_]
    OR,           // |
    STAR,         // *
    PLUS,         // +
    QMARK,        // ?
    LPAREN,       // (
    RPAREN,       // )
    CONCAT        // 连接符，内部使用
};

struct ReToken
{
    ReTokenType type;
    char val;
    std::vector<char> char_set;

    ReToken(ReTokenType t, char v) : type(t), val(v) {}
    ReToken(const std::vector<char>& s) : type(CHAR_CLASS), val('\0'), char_set(s) {}
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

bool is_regex_op(char ch)
{
    return (ch == '|' || ch == '(' ||
            ch == ')' || ch == '*' ||
            ch == '+' || ch == '?');
}

bool is_atom_end(const ReToken& t)
{
    return t.type == LITERAL   ||
           t.type == CHAR_CLASS ||
           t.type == RPAREN    ||
           t.type == QMARK     ||
           t.type == PLUS      ||
           t.type == STAR;
}

bool is_atom_begin(const ReToken& t)
{
    return t.type == LITERAL ||
           t.type == CHAR_CLASS ||
           t.type == LPAREN;
}

// 判断两个正则 token 之间是否需要连接符
bool need_concat(const ReToken& a, const ReToken& b)
{
    return is_atom_end(a) && is_atom_begin(b);
}

std::vector<char> parse_char_class(const std::string& re, size_t& i)
{
    // 当前 re[i] == '['
    i++;
    if(i >= re.size())
    {
        logger::error("unterminated char class");
        std::exit(EXIT_FAILURE);
    }

    std::set<char> chars;
    bool has_any = false;
    while(i < re.size() && re[i] != ']')
    {
        if(i + 2 < re.size() && re[i + 1] == '-' && re[i + 2] != ']')
        {
            char l = re[i];
            char r = re[i + 2];
            if(l > r)
            {
                logger::error("invalid range in char class");
                std::exit(EXIT_FAILURE);
            }
            for(char c = l; c <= r; ++c)
            {
                chars.insert(c);
            }
            i += 3;
            has_any = true;
        }
        else
        {
            chars.insert(re[i]);
            i++;
            has_any = true;
        }
    }

    if(i >= re.size() || re[i] != ']')
    {
        logger::error("unterminated char class");
        std::exit(EXIT_FAILURE);
    }

    if(!has_any)
    {
        logger::error("empty char class");
        std::exit(EXIT_FAILURE);
    }

    std::vector<char> result(chars.begin(), chars.end());
    return result;
}

std::vector<ReToken> tokenize_regex(const std::string& re)
{
    logger::debug("tokenize_regex begin: ", re);

    std::vector<ReToken> output;
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
        else if(is_regex_op(ch))
        {
            switch(ch)
            {
                case '|':
                    output.emplace_back(OR, ch);
                    break;
                case '(':
                    output.emplace_back(LPAREN, ch);
                    break;
                case ')':
                    output.emplace_back(RPAREN, ch);
                    break;
                case '*':
                    output.emplace_back(STAR, ch);
                    break;
                case '?':
                    output.emplace_back(QMARK, ch);
                    break;
                case '+':
                    output.emplace_back(PLUS, ch);
                    break;
                default:
                    break;
            }
        }
        else
        {
            output.emplace_back(LITERAL, ch);
        }
    }

    logger::debug("tokenize_regex token count: ", output.size());
    return output;
}

std::vector<ReToken> insert_concat(const std::vector<ReToken>& re)
{
    logger::debug("insert_concat input token count: ", re.size());

    if(re.empty()) return {};

    std::vector<ReToken> output;
    output.emplace_back(re[0]);
    for(size_t i = 1; i < re.size(); ++i)
    {
        if(need_concat(re[i - 1], re[i]))
        {
            output.emplace_back(CONCAT, '\0');
        }
        output.emplace_back(re[i]);
    }

    logger::debug("insert_concat output token count: ", output.size());
    return output;
}

int re_op_prio(const ReToken& t)
{
    switch(t.type)
    {
        case OR:
            return 1;
        case CONCAT:
            return 2;
        case STAR:
        case PLUS:
        case QMARK:
            return 3;
        default:
            return 0;
    }
}

std::vector<ReToken> infix_to_postfix(const std::vector<ReToken>& infix)
{
    logger::debug("infix_to_postfix input token count: ", infix.size());
    std::vector<ReToken> output;
    std::stack<ReToken> ops;
    for(const auto& token : infix)
    {
        if(token.type == LITERAL || token.type == CHAR_CLASS)
        {
            output.emplace_back(token);
        }else if(token.type == LPAREN)
        {
            ops.push(token);
        }else if(token.type == RPAREN){
            while(!ops.empty() && ops.top().type != LPAREN)
            {
                output.emplace_back(ops.top());
                ops.pop();
            }
            if(!ops.empty() && ops.top().type == LPAREN)
            {
                ops.pop();
            }else
            {
                logger::error("mismatched parentheses");
                std::exit(EXIT_FAILURE);
            }
        }else if(token.type == OR || token.type == CONCAT || token.type == STAR || token.type == PLUS || token.type == QMARK){
            while(!ops.empty() && ops.top().type != LPAREN && re_op_prio(ops.top()) >= re_op_prio(token))
            {
                output.emplace_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        }else{
            logger::error("invalid token in infix_to_postfix");
            std::exit(EXIT_FAILURE);
        }
    }
    while(!ops.empty())
    {
        if(ops.top().type == LPAREN || ops.top().type == RPAREN)
        {
            logger::error("mismatched parentheses");
            std::exit(EXIT_FAILURE);
        }
        output.emplace_back(ops.top());
        ops.pop();
    }
    logger::debug("infix_to_postfix output token count: ", output.size());
    return output;
}


// ==================================== 后缀正则转NFA

static constexpr int EPS = -1;
static constexpr int ASCII_COUNT = 128;

struct Edge
{
    int to;
    int sym;   // 普通字符或 EPS
};

struct State
{
    std::vector<Edge> edges;
    int token_kind = 0;   // 0 表示不是接受状态
    int priority = std::numeric_limits<int>::max();
};

struct NFA
{
    std::vector<State> states;
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

void add_nfa_edge(NFA& nfa, int from, int to, int sym)
{
    nfa.states[from].edges.push_back({to, sym});
}

NFA build_nfa_from_postfix(const std::vector<ReToken>& postfix)
{
    logger::debug("build_nfa_from_postfix input token count: ", postfix.size());

    NFA nfa;
    std::stack<NFAFragment> st;

    for(const auto& token : postfix)
    {
        if(token.type == LITERAL)
        {
            int s = new_nfa_state(nfa);
            int t = new_nfa_state(nfa);
            add_nfa_edge(nfa, s, t, token.val);
            st.push({s, t});
        }else if(token.type == CHAR_CLASS){
            int s = new_nfa_state(nfa);
            int t = new_nfa_state(nfa);
            for(const auto& ch : token.char_set)
            {
                add_nfa_edge(nfa, s, t, ch);
            }
            st.push({s, t});
        }else if(token.type == CONCAT){
            NFAFragment r = st.top();
            st.pop();
            NFAFragment l = st.top();
            st.pop();
            add_nfa_edge(nfa, l.accept, r.start, EPS);
            st.push({l.start, r.accept});
        }else if(token.type == OR){
            if(st.size() < 2)
            {
                logger::error("invalid regex: OR needs two operands");
                std::exit(EXIT_FAILURE);
            }
            NFAFragment rhs = st.top();
            st.pop();
            NFAFragment lhs = st.top();
            st.pop();
            int s = new_nfa_state(nfa);
            int t = new_nfa_state(nfa);
            add_nfa_edge(nfa, s, lhs.start, EPS);
            add_nfa_edge(nfa, s, rhs.start, EPS);
            add_nfa_edge(nfa, lhs.accept, t, EPS);
            add_nfa_edge(nfa, rhs.accept, t, EPS);
            st.push({s, t});
        }else if(token.type == STAR){
            if(st.empty())
            {
                logger::error("invalid regex: STAR needs one operand");
                std::exit(EXIT_FAILURE);
            }
            NFAFragment sub = st.top();
            st.pop();
            int s = new_nfa_state(nfa);
            int t = new_nfa_state(nfa);
            add_nfa_edge(nfa, s, sub.start, EPS);
            add_nfa_edge(nfa, s, t, EPS);
            add_nfa_edge(nfa, sub.accept, sub.start, EPS);
            add_nfa_edge(nfa, sub.accept, t, EPS);
            st.push({s, t});
        }else if(token.type == PLUS){
            if(st.empty())
            {
                logger::error("invalid regex: PLUS needs one operand");
                std::exit(EXIT_FAILURE);
            }
            NFAFragment sub = st.top(); st.pop();
            int s = new_nfa_state(nfa);
            int t = new_nfa_state(nfa);
            add_nfa_edge(nfa, s, sub.start, EPS);
            add_nfa_edge(nfa, sub.accept, sub.start, EPS);
            add_nfa_edge(nfa, sub.accept, t, EPS);
            st.push({s, t});
        }else if(token.type == QMARK){
            if(st.empty())
            {
                logger::error("invalid regex: QMARK needs one operand");
                std::exit(EXIT_FAILURE);
            }
            NFAFragment sub = st.top(); st.pop();
            int s = new_nfa_state(nfa);
            int t = new_nfa_state(nfa);
            add_nfa_edge(nfa, s, sub.start, EPS);
            add_nfa_edge(nfa, s, t, EPS);
            add_nfa_edge(nfa, sub.accept, t, EPS);
            st.push({s, t});
        }else
        {
            logger::error("invalid token in postfix NFA construction");
            std::exit(EXIT_FAILURE);
        }
    }

    if(st.size() != 1)
    {
        logger::error("invalid regex: malformed postfix expression");
        std::exit(EXIT_FAILURE);
    }
    NFAFragment res = st.top();
    nfa.start = res.start;
    nfa.accept = res.accept;
    logger::debug("build_nfa_from_postfix state count: ", nfa.states.size());
    return nfa;
}

NFA regex_to_nfa(const std::string& re)
{
    std::vector<ReToken> tokens = tokenize_regex(re);
    std::vector<ReToken> infix = insert_concat(tokens);
    std::vector<ReToken> postfix = infix_to_postfix(infix);
    return build_nfa_from_postfix(postfix);
}

// ==================================== 合并多条词法规则

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

void mark_nfa_accept(NFA& nfa, int token_kind, int priority)
{
    nfa.states[nfa.accept].token_kind = token_kind;
    nfa.states[nfa.accept].priority = priority;
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

NFA char_set_plus_to_nfa(const std::vector<char>& chars)
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

bool is_escape_body_char(int ch)
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
    add_ascii_edges_if(nfa, body, body, is_escape_body_char);

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
    add_ascii_edges_if(nfa, esc, body, is_escape_body_char);
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
    add_ascii_edges_if(nfa, esc, one_char, is_escape_body_char);
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

NFA make_regex_rule(int kind, int priority, const std::string& re)
{
    NFA nfa = regex_to_nfa(re);
    mark_nfa_accept(nfa, kind, priority);
    return nfa;
}

NFA make_literal_rule(int kind, int priority, const std::string& text)
{
    NFA nfa = literal_to_nfa(text);
    mark_nfa_accept(nfa, kind, priority);
    return nfa;
}

NFA make_char_set_plus_rule(int kind, int priority, const std::vector<char>& chars)
{
    NFA nfa = char_set_plus_to_nfa(chars);
    mark_nfa_accept(nfa, kind, priority);
    return nfa;
}

NFA make_nfa_rule(int kind, int priority, NFA nfa)
{
    mark_nfa_accept(nfa, kind, priority);
    return nfa;
}

NFA combine_rules_to_nfa(const std::vector<NFA>& rules)
{
    logger::debug("combine_rules_to_nfa rule count: ", rules.size());

    NFA combined;
    combined.start = new_nfa_state(combined);

    for(const auto& rule : rules)
    {
        int offset = combined.states.size();
        for(const auto& st : rule.states)
        {
            combined.states.push_back(st);
        }
        for(size_t i = offset; i < combined.states.size(); ++i)
        {
            for(auto& e : combined.states[i].edges)
            {
                e.to += offset;
            }
        }
        add_nfa_edge(combined, combined.start, rule.start + offset, EPS);
    }

    logger::debug("combine_rules_to_nfa state count: ", combined.states.size());
    return combined;
}

// ==================================== NFA 子集构造为 DFA

struct DFAState
{
    std::vector<int> trans;
    int token_kind = TK_NONE;
    std::set<int> nfa_states;

    DFAState() : trans(ASCII_COUNT, -1) {}
};

struct DFA
{
    std::vector<DFAState> states;
    int start = -1;
};

std::set<int> epsilon_closure(const NFA& nfa, const std::set<int>& input)
{
    std::set<int> result = input;
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
            if(e.sym == EPS && result.count(e.to) == 0)
            {
                result.insert(e.to);
                st.push(e.to);
            }
        }
    }
    return result;
}

std::set<int> move_set(const NFA& nfa, const std::set<int>& input, int sym)
{
    std::set<int> result;
    for(int s : input)
    {
        for(const auto& e : nfa.states[s].edges)
        {
            if(e.sym == sym)
            {
                result.insert(e.to);
            }
        }
    }
    return result;
}

void set_dfa_accept_from_nfa_states(DFAState& dstate, const NFA& nfa)
{
    int best_priority = std::numeric_limits<int>::max();
    for(int s : dstate.nfa_states)
    {
        const State& nstate = nfa.states[s];
        if(nstate.token_kind != TK_NONE && nstate.priority < best_priority)
        {
            dstate.token_kind = nstate.token_kind;
            best_priority = nstate.priority;
        }
    }
}

int add_dfa_state(DFA& dfa, const NFA& nfa, const std::set<int>& nfa_states)
{
    DFAState dstate;
    dstate.nfa_states = nfa_states;
    set_dfa_accept_from_nfa_states(dstate, nfa);
    dfa.states.push_back(dstate);
    return dfa.states.size() - 1;
}

DFA nfa_to_dfa(const NFA& nfa)
{
    logger::debug("nfa_to_dfa input state count: ", nfa.states.size());

    DFA dfa;
    std::map<std::set<int>, int> ids;
    std::queue<std::set<int>> work;

    std::set<int> start_input;
    start_input.insert(nfa.start);
    std::set<int> start_set = epsilon_closure(nfa, start_input);

    dfa.start = add_dfa_state(dfa, nfa, start_set);
    ids[start_set] = dfa.start;
    work.push(start_set);

    while(!work.empty())
    {
        std::set<int> cur_set = work.front();
        work.pop();
        int cur_id = ids[cur_set];

        for(int sym = 0; sym < ASCII_COUNT; ++sym)
        {
            std::set<int> moved = move_set(nfa, cur_set, sym);
            if(moved.empty())
            {
                continue;
            }

            std::set<int> next_set = epsilon_closure(nfa, moved);
            if(ids.count(next_set) == 0)
            {
                int next_id = add_dfa_state(dfa, nfa, next_set);
                ids[next_set] = next_id;
                work.push(next_set);
            }
            dfa.states[cur_id].trans[sym] = ids[next_set];
        }
    }

    logger::debug("nfa_to_dfa output state count: ", dfa.states.size());
    return dfa;
}

// ==================================== DFA 最小化

std::vector<int> build_minimize_signature(const DFA& dfa, const std::vector<int>& cls, int state)
{
    std::vector<int> sig;
    sig.push_back(dfa.states[state].token_kind);
    for(int sym = 0; sym < ASCII_COUNT; ++sym)
    {
        int to = dfa.states[state].trans[sym];
        sig.push_back(to == -1 ? -1 : cls[to]);
    }
    return sig;
}

DFA minimize_dfa(const DFA& dfa)
{
    logger::debug("minimize_dfa input state count: ", dfa.states.size());

    int n = dfa.states.size();
    std::vector<int> cls(n, 0);
    std::map<int, int> token_class;
    int class_count = 0;

    for(int i = 0; i < n; ++i)
    {
        int token = dfa.states[i].token_kind;
        if(token_class.count(token) == 0)
        {
            token_class[token] = class_count++;
        }
        cls[i] = token_class[token];
    }

    bool changed = true;
    while(changed)
    {
        changed = false;
        std::map<std::vector<int>, int> sig_class;
        std::vector<int> next_cls(n, 0);
        int next_count = 0;

        for(int i = 0; i < n; ++i)
        {
            std::vector<int> sig = build_minimize_signature(dfa, cls, i);
            if(sig_class.count(sig) == 0)
            {
                sig_class[sig] = next_count++;
            }
            next_cls[i] = sig_class[sig];
        }

        if(next_cls != cls)
        {
            changed = true;
            cls = next_cls;
            class_count = next_count;
        }
    }

    DFA min_dfa;
    min_dfa.states.resize(class_count);
    min_dfa.start = cls[dfa.start];

    std::vector<bool> filled(class_count, false);
    for(int i = 0; i < n; ++i)
    {
        int c = cls[i];
        if(filled[c])
        {
            continue;
        }

        filled[c] = true;
        min_dfa.states[c].token_kind = dfa.states[i].token_kind;
        for(int sym = 0; sym < ASCII_COUNT; ++sym)
        {
            int to = dfa.states[i].trans[sym];
            min_dfa.states[c].trans[sym] = to == -1 ? -1 : cls[to];
        }
    }

    logger::debug("minimize_dfa output state count: ", min_dfa.states.size());
    return min_dfa;
}

// ==================================== 最小 C 词法规则和扫描器

std::vector<NFA> build_min_c_rules()
{
    std::vector<NFA> rules;
    int p = 1;

    auto literal = [&](int kind, const std::string& text) {
        rules.push_back(make_literal_rule(kind, p++, text));
    };
    auto regex = [&](int kind, const std::string& re) {
        rules.push_back(make_regex_rule(kind, p++, re));
    };
    auto raw_nfa = [&](int kind, NFA nfa) {
        rules.push_back(make_nfa_rule(kind, p++, nfa));
    };

    rules.push_back(make_char_set_plus_rule(TK_SKIP, p++, {' ', '\t', '\n', '\r', '\f', '\v'}));
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

            int next = dfa.states[state].trans[ch];
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
