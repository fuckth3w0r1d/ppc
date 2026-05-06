# Lexer 学习笔记

本文对应 `src/lexer.cpp`，用于对照代码学习编译原理里的词法分析流程。

这个 lexer 是教学用的最小实现，不追求完整 C 语言标准。它保留了词法分析最重要的自动机主线：

```text
词法规则
  -> 正则表达式 / 手写 NFA
  -> 合并 NFA
  -> 子集构造 DFA
  -> DFA 最小化
  -> 最长匹配扫描源码
  -> 输出 token 流
```

## 1. 词法分析做什么

词法分析器把源代码字符流切成 token 流。

例如输入：

```c
float x = 12.34;
```

输出类似：

```text
1:1   KW_FLOAT  float
1:7   IDENT     x
1:9   ASSIGN    =
1:11  FLOAT     12.34
1:16  SEMI      ;
```

词法分析只负责识别“单词”。它不判断这些 token 能不能组成合法语句，那是语法分析阶段的任务。

## 2. 当前支持的 token

token 类型定义在 `TokenKind`：

```cpp
enum TokenKind
{
    TK_NONE = 0,
    TK_SKIP,
    TK_KW_INT,
    TK_IDENT,
    TK_NUMBER,
    TK_FLOAT,
    ...
};
```

其中：

- `TK_NONE` 表示非接受状态。
- `TK_SKIP` 表示识别后丢弃，比如空白和注释。
- `TK_KW_*` 表示关键字。
- `TK_IDENT` 表示标识符。
- `TK_NUMBER` 表示整数。
- `TK_FLOAT` 表示教学版浮点数。
- `TK_STRING` 和 `TK_CHAR_LITERAL` 表示字符串和字符字面量。
- `TK_EQ`、`TK_ASSIGN`、`TK_LPAREN` 等表示运算符和分隔符。

输出 token 时使用：

```cpp
struct Lexeme
{
    int token_kind;
    std::string text;
    int line;
    int col;
};
```

也就是每个 token 保存：

```text
类型 + 原始文本 + 行号 + 列号
```

## 3. 规则入口：build_min_c_rules

当前所有词法规则集中在：

```cpp
std::vector<NFA> build_min_c_rules()
```

规则按顺序加入：

```cpp
raw_nfa(TK_SKIP, one_or_more_chars_nfa(...));
raw_nfa(TK_SKIP, line_comment_to_nfa());
raw_nfa(TK_SKIP, block_comment_to_nfa());

literal(TK_KW_INT, "int");
literal(TK_KW_RETURN, "return");

regex(TK_IDENT, "[a-zA-Z_]([a-zA-Z0-9_])*");
raw_nfa(TK_FLOAT, float_literal_to_nfa());
regex(TK_NUMBER, "[0-9]+");
```

这里有三种规则来源：

```text
literal: 固定字符串，比如 int、return、==、;
regex: 简化正则表达式，比如标识符、整数
raw_nfa: 手写 NFA，比如注释、字符串、字符、浮点
```

每条规则最终都会变成一个 NFA。

## 4. 规则优先级

规则加入时有一个递增的优先级：

```cpp
int p = 1;
```

越早加入，优先级越高。

规则优先级写在 NFA 的接受状态上：

```cpp
NFA token_nfa(int token_kind, int rule_priority, NFA nfa)
{
    nfa.states[nfa.accept].token_kind = token_kind;
    nfa.states[nfa.accept].rule_priority = rule_priority;
    return nfa;
}
```

为什么需要优先级？

例如：

```c
int
```

既可以匹配关键字 `int`，也可以匹配标识符规则：

```text
[a-zA-Z_]([a-zA-Z0-9_])*
```

当同一个 DFA 状态同时对应多个 NFA 接受状态时，用优先级决定 token 类型。

注意：优先级不是用来代替最长匹配的。最长匹配仍然优先。优先级只处理“同样长度时多个规则都接受”的情况。

## 5. 正则 token 化

代码从简化正则开始：

```cpp
std::vector<RegexToken> tokenize_regex(const std::string& re)
```

支持的正则元素包括：

```text
普通字符
字符类 [a-zA-Z_]
|
*
+
?
()
```

正则 token 类型定义在：

```cpp
enum RegexTokenType
{
    RE_LITERAL,
    RE_CHAR_CLASS,
    RE_OR,
    RE_STAR,
    RE_PLUS,
    RE_OPTIONAL,
    RE_LPAREN,
    RE_RPAREN,
    RE_CONCAT
};
```

`RE_CONCAT` 是内部使用的连接符。用户写正则时不写它。

例如：

```text
ab
```

会被理解成：

```text
a . b
```

其中 `.` 对应 `RE_CONCAT`。

## 6. 插入连接符

函数：

```cpp
std::vector<RegexToken> insert_concat(const std::vector<RegexToken>& re)
```

负责把隐式连接变成显式连接。

例如：

```text
[a-zA-Z_]([a-zA-Z0-9_])*
```

其中：

```text
[a-zA-Z_] 后面紧跟 (
```

这两个正则项之间需要连接符。

判断逻辑：

```cpp
bool need_concat(const RegexToken& a, const RegexToken& b)
{
    return is_atom_end(a) && is_atom_begin(b);
}
```

大致含义：

```text
如果 a 可以作为一个原子结束
并且 b 可以作为一个原子开始
那么 a 和 b 之间需要连接
```

## 7. 中缀正则转后缀正则

函数：

```cpp
std::vector<RegexToken> infix_to_postfix(const std::vector<RegexToken>& infix)
```

正则原本是中缀表达式：

```text
a(b|c)*
```

后缀表达式更适合用栈构造 NFA：

```text
a b c | * .
```

代码使用类似“调度场算法”的方法：

- 操作数直接输出。
- 左括号入栈。
- 右括号弹出直到左括号。
- 运算符按优先级出栈。

优先级由 `regex_precedence()` 给出：

```text
|       最低
连接    中间
* + ?   最高
```

## 8. NFA 数据结构

NFA 表示非确定有限自动机。

它的特点：

- 一个状态读同一个字符，可以去多个状态。
- 可以有 `EPS` 边，不消耗字符直接跳转。

代码中：

```cpp
static constexpr int EPS = -1;
```

NFA 边：

```cpp
struct NFAEdge
{
    int to;
    int symbol;   // 普通字符或 EPS
};
```

NFA 状态：

```cpp
struct NFAState
{
    std::vector<NFAEdge> edges;
    int token_kind = TK_NONE;
    int rule_priority = std::numeric_limits<int>::max();
};
```

普通 NFA 状态不是接受状态，所以 `token_kind` 默认为 `TK_NONE`。

如果某个状态是某条词法规则的接受状态，才会写入：

```text
token_kind
rule_priority
```

NFA 本体：

```cpp
struct NFA
{
    std::vector<NFAState> states;
    int start = -1;
    int accept = -1;
};
```

## 9. NFAFragment 是什么

Thompson 构造会不断组合小 NFA 片段。

片段只需要知道起点和终点：

```cpp
struct NFAFragment
{
    int start;
    int accept;
};
```

例如单个字符 `a` 的片段：

```text
start --a--> accept
```

函数：

```cpp
NFAFragment symbol_frag(NFA& nfa, int symbol)
{
    int start = new_nfa_state(nfa);
    int accept = new_nfa_state(nfa);
    add_nfa_edge(nfa, start, accept, symbol);
    return {start, accept};
}
```

它是构造 NFA 的最小积木。

## 10. Thompson 构造：后缀正则转 NFA

入口：

```cpp
NFA build_nfa_from_postfix(const std::vector<RegexToken>& postfix)
```

核心思想：用栈处理后缀正则。

遇到操作数：

```text
RE_LITERAL
RE_CHAR_CLASS
```

就生成一个基本 NFA 片段并入栈。

遇到操作符：

```text
RE_CONCAT
RE_OR
RE_STAR
RE_PLUS
RE_OPTIONAL
```

就从栈里弹出一个或两个片段，组合后再压回栈。

### 10.1 单字符

```cpp
symbol_frag()
```

构造：

```text
start --symbol--> accept
```

### 10.2 字符类

```cpp
char_class_frag()
```

比如 `[abc]`：

```text
        --a--
start  --b--  accept
        --c--
```

### 10.3 连接

```cpp
concat_frag()
```

把两个片段首尾相连：

```text
A.accept --EPS--> B.start
```

### 10.4 或

```cpp
or_frag()
```

对应 `A|B`：

```text
          EPS -> A -> EPS
start                       accept
          EPS -> B -> EPS
```

### 10.5 闭包

```cpp
star_frag()
```

对应 `A*`，表示零次或多次。

### 10.6 一次或多次

```cpp
plus_frag()
```

对应 `A+`，表示一次或多次。

### 10.7 可选

```cpp
optional_frag()
```

对应 `A?`，表示零次或一次。

## 11. 手写 NFA

有些规则用当前简化正则表达不方便，所以直接手写 NFA。

例如：

```cpp
line_comment_to_nfa()
block_comment_to_nfa()
string_literal_to_nfa()
char_literal_to_nfa()
float_literal_to_nfa()
```

这些仍然属于自动机构造，没有绕过 NFA/DFA 主线。

例如行注释：

```text
// 后面一直读到换行前
```

字符串字面量：

```text
" 普通字符或转义字符 "
```

字符字面量：

```text
'a'
'\n'
```

浮点数支持教学子集：

```text
12.34
.5
10.
```

## 12. 多规则 NFA 合并

每条词法规则都会生成一个 NFA。

词法分析时不能一条一条慢慢试，而是要把所有规则合成一个大 NFA。

入口：

```cpp
NFA combine_rules_to_nfa(const std::vector<NFA>& rules)
```

做法：

```text
创建一个新的总 start
从总 start 用 EPS 边连到每条规则的 start
```

示意：

```text
                EPS -> rule 1
combined.start  EPS -> rule 2
                EPS -> rule 3
                ...
```

这样从一个总开始状态出发，就等价于“同时尝试所有 token 规则”。

`append_nfa()` 负责把一个 NFA 的状态复制到另一个 NFA 里，并修正状态编号。

## 13. DFA 数据结构

DFA 是确定有限自动机。

特点：

- 没有 EPS 边。
- 一个状态读一个字符，最多只有一个下一个状态。
- 扫描时速度快。

代码中：

```cpp
struct DFAState
{
    std::vector<int> next;
    int token_kind = TK_NONE;
};
```

`next[ch]` 表示读入字符 `ch` 后去哪个状态。

如果 `next[ch] == -1`，说明没有这条转移。

## 14. 子集构造：NFA 转 DFA

入口：

```cpp
DFA nfa_to_dfa(const NFA& nfa)
```

核心思想：

```text
DFA 的一个状态 = NFA 的一组状态
```

代码中：

```cpp
using StateSet = std::set<int>;
```

关键函数：

```cpp
epsilon_closure()
move_set()
```

### 14.1 epsilon_closure

```cpp
StateSet epsilon_closure(const NFA& nfa, const StateSet& input)
```

含义：

```text
从 input 状态集合出发，只走 EPS 边，能到达的所有 NFA 状态。
```

### 14.2 move_set

```cpp
StateSet move_set(const NFA& nfa, const StateSet& input, int symbol)
```

含义：

```text
从 input 状态集合出发，读入 symbol，能到达的 NFA 状态。
```

### 14.3 子集构造流程

伪代码：

```text
start_set = epsilon_closure({nfa.start})
创建 DFA 开始状态 start_set

while 还有未处理的 DFA 状态集合:
    for 每个 ASCII 字符 symbol:
        moved = move_set(cur_set, symbol)
        next_set = epsilon_closure(moved)
        如果 next_set 没见过，创建新 DFA 状态
        添加 DFA 转移 cur --symbol--> next
```

## 15. DFA 接受状态如何确定

一个 DFA 状态对应一组 NFA 状态。

只要这个集合里存在 NFA 接受状态，那么这个 DFA 状态就是接受状态。

函数：

```cpp
int choose_token_kind(const NFA& nfa, const StateSet& nfa_states)
```

如果集合里有多个 NFA 接受状态，就选 `rule_priority` 最小的。

这解决了同长度冲突，例如：

```text
int 可以是 KW_INT，也可以是 IDENT
```

因为关键字规则先加入，优先级更高，所以 `int` 输出为 `KW_INT`。

## 16. DFA 最小化

入口：

```cpp
DFA minimize_dfa(const DFA& dfa)
```

DFA 最小化的目标：

```text
合并行为完全等价的状态。
```

当前实现使用划分细化。

初始划分：

```text
按 token_kind 分组
```

原因：接受不同 token 的状态不能合并。

然后不断根据转移行为细化分组。

每个状态会构造一个签名：

```cpp
build_minimize_signature()
```

签名内容：

```text
当前状态的 token_kind
每个输入字符会跳到哪个分组
```

如果两个状态签名相同，它们暂时可以在同一组。

重复这个过程，直到分组不再变化。

## 17. 扫描源码：最长匹配

入口：

```cpp
std::vector<Lexeme> scan_source(const DFA& dfa, const std::string& source)
```

核心变量：

```cpp
size_t last_accept_pos;
int last_accept_kind;
```

扫描逻辑：

```text
从当前位置 pos 开始
沿 DFA 一直往前走
每次到达接受状态，就记录 last_accept_pos 和 last_accept_kind
直到不能继续转移
```

如果从未到达接受状态，说明词法错误。

否则取最后一次接受的位置作为 token 结束位置。

这就是最长匹配。

例如：

```c
intx
```

虽然前 3 个字符 `int` 可以匹配关键字，但继续读 `x` 后，整个 `intx` 可以匹配标识符，所以最终输出：

```text
IDENT intx
```

## 18. TK_SKIP

有些规则需要识别，但不需要输出。

例如：

```text
空白
行注释
块注释
```

它们的 token 类型是：

```cpp
TK_SKIP
```

扫描时：

```cpp
if(last_accept_kind != TK_SKIP)
{
    tokens.push_back(...);
}
```

因此它们参与匹配和行列更新，但不会写入 token 文件。

## 19. 文件输入输出

程序入口：

```cpp
int main(int argc, char** argv)
```

用法：

```bash
bin/lexer <input.c> <output.tokens>
```

流程：

```text
read_file()
build_min_c_rules()
combine_rules_to_nfa()
nfa_to_dfa()
minimize_dfa()
scan_source()
print_tokens()
```

示例：

```bash
bin/lexer test/sample.c test/sample.tokens
```

## 20. 推荐阅读顺序

建议按下面顺序读 `src/lexer.cpp`：

1. `build_min_c_rules()`
2. `TokenKind` 和 `Lexeme`
3. `regex_to_nfa()`
4. `build_nfa_from_postfix()`
5. `combine_rules_to_nfa()`
6. `nfa_to_dfa()`
7. `minimize_dfa()`
8. `scan_source()`
9. `main()`

这个顺序比从文件第一行读更适合学习。

## 21. 当前实现的边界

这个 lexer 是教学版，故意没有完整实现 C 标准。

已支持：

```text
关键字
标识符
整数
教学版浮点数
字符串字面量
字符字面量
行注释
块注释
常见运算符和分隔符
```

未完整支持：

```text
预处理指令
宏展开
十六进制整数
十六进制浮点
指数浮点
宽字符 / 宽字符串
完整 C 转义序列校验
嵌套注释
```

这些不是当前重点。当前重点是把词法分析里的自动机流程讲清楚。

## 22. 一句话总结

这个 lexer 的核心不是 C，而是这条编译原理主线：

```text
用正则描述 token
用 NFA 表达正则
用 DFA 高效扫描
用最长匹配和优先级决定最终 token
```
