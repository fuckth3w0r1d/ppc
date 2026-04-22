#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_set>
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

bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c == '_');
}

bool is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

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

bool is_atom_token(const ReToken& t)
{
    return t.type == LITERAL || t.type == CHAR_CLASS;
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

struct Edge
{
    int to;
    int sym;   // 普通字符或 EPS
};

struct State
{
    std::vector<Edge> edges;
};

struct NFA
{
    std::vector<State> states;
    int start;
    int accept;
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

void print_postfix(const std::vector<ReToken>& postfix)
{
    for(const auto& token : postfix)
    {
        switch(token.type)
        {
            case LITERAL:
                std::cout << token.val << ' ';
                break;
            case CHAR_CLASS:
                std::cout << "[class] ";
                break;
            case OR:
                std::cout << "| ";
                break;
            case STAR:
                std::cout << "* ";
                break;
            case PLUS:
                std::cout << "+ ";
                break;
            case QMARK:
                std::cout << "? ";
                break;
            case CONCAT:
                std::cout << ". ";
                break;
            default:
                break;
        }
    }
    std::cout << '\n';
}

void print_nfa(const NFA& nfa)
{
    std::cout << "start: " << nfa.start << '\n';
    std::cout << "accept: " << nfa.accept << '\n';

    for(size_t i = 0; i < nfa.states.size(); ++i)
    {
        for(const auto& e : nfa.states[i].edges)
        {
            std::cout << i << " -> " << e.to << " : ";
            if(e.sym == EPS)
                std::cout << "EPS";
            else
                std::cout << static_cast<char>(e.sym);
            std::cout << '\n';
        }
    }
}

int main()
{
    std::string re = "a(b|c)*d[0-9]?";

    std::vector<ReToken> tokens = tokenize_regex(re);
    std::vector<ReToken> infix = insert_concat(tokens);
    std::vector<ReToken> postfix = infix_to_postfix(infix);

    std::cout << "postfix: ";
    print_postfix(postfix);

    NFA nfa = build_nfa_from_postfix(postfix);
    print_nfa(nfa);

    return 0;
}
