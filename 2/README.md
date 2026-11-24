# 活动投票封存

面向活动运营平台的**持续运行 HTTP 服务**：创建投票、首次投票 / 改票、汇总票数、结票固定结果，保证「票数、有效投票人数、最终排名」三者始终一致。

---

## 目录

- [1. 快速开始](#1-快速开始)
- [2. 数据模型](#2-数据模型)
- [3. 接口说明](#3-接口说明)
- [4. 业务规则](#4-业务规则)
- [5. 边界与校验汇总](#5-边界与校验汇总)
- [6. 示例流程](#6-示例流程)
- [7. 本地自测](#7-本地自测)
- [8. 实现要点](#8-实现要点)

---

## 1. 快速开始

### 1.1 环境与启动要求

| 项 | 约定 |
| --- | --- |
| 判题端口 | 固定 `8000`，监听 `0.0.0.0` |
| PORT 变量 | 可读取 `PORT`，默认必须为 `8000` |
| 入口文件 | 位于当前 `work/` 根目录 |
| 运行方式 | 前台持续运行 |
| 启动状态 | 等同于已执行一次 `POST /reset` |
| 依赖 | 标准库即可，无需外网 |

### 1.2 复制预设代码

只复制所选语言对应目录到 `work/` 根目录：

```bash
# Python
cp -R baseline/python/. .

# 其他语言同理
cp -R baseline/go/. .
cp -R baseline/javascript/. .
cp -R baseline/typescript/. .
cp -R baseline/java/. .
cp -R baseline/cpp/. .
```

| 语言 | 目录 | 入口 |
| --- | --- | --- |
| Python | `baseline/python/` | `main.py` |
| Go | `baseline/go/` | `main.go`、`go.mod` |
| JavaScript | `baseline/javascript/` | `main.js` |
| TypeScript | `baseline/typescript/` | `main.ts`、`tsconfig.json` |
| Java | `baseline/java/` | `Main.java` |
| C++ | `baseline/cpp/` | `main.cpp` |

### 1.3 启动命令

| 语言 | 命令 |
| --- | --- |
| Python | `python3 main.py` |
| Go | `go build . && ./<binary>` |
| JavaScript | `node main.js` |
| TypeScript | `tsx main.ts` |
| Java | `javac Main.java && java Main` |
| C++ | `g++ -O2 -std=c++17 -pthread main.cpp -o main && ./main` |

```bash
# Python 示例
PORT=8000 python3 main.py &
python3 sample_testcases/run_sample_testcases.py
```

---

## 2. 数据模型

### Poll：一场活动投票

| 字段 | 含义 | 类型 / 约束 | 读写 |
| --- | --- | --- | --- |
| `id` | 服务端生成，按创建顺序为 `poll_1`、`poll_2`…… | string | 只读 |
| `title` | 投票标题，去除首尾空白后长度 1~80 | string | 创建时必填 |
| `status` | `open` 或 `closed` | enum | 服务端维护 |
| `results` | 结果数组，每项 `{"option": string, "votes": int}` | array | 服务端维护 |
| `total_votes` | 当前有效投票人数 | 非负整数 | 只读 |

> `votes` 单位为票；`total_votes` 单位为票，等于该场投票的有效投票用户数。

**results 排序规则：** 票数**降序**；票数相同时按候选项在**创建请求中的原始顺序**排列。

---

## 3. 接口说明

所有请求体与响应体均为 JSON。

| 方法 | 路径 | 功能 | 成功响应 |
| --- | --- | --- | --- |
| `POST` | `/polls` | 创建投票 | 完整 `Poll` |
| `PUT` | `/polls/{poll_id}/votes/{voter_id}` | 投票 / 改票 | 完整 `Poll` |
| `POST` | `/polls/{poll_id}/close` | 结束投票 | 完整 `Poll` |
| `POST` | `/reset` | 清空数据 | `{"ok": true}` |

### 3.1 创建投票 — `POST /polls`

请求：

```json
{
  "title": "午餐选择",
  "options": ["米饭", "面条"]
}
```

响应：

```json
{
  "id": "poll_1",
  "title": "午餐选择",
  "status": "open",
  "results": [
    { "option": "米饭", "votes": 0 },
    { "option": "面条", "votes": 0 }
  ],
  "total_votes": 0
}
```

### 3.2 投票 / 改票 — `PUT /polls/{poll_id}/votes/{voter_id}`

请求：

```json
{ "option": "米饭" }
```

- 首次投票 → 该候选项 `+1`，`total_votes +1`
- 改票 → 旧候选项 `-1`，新候选项 `+1`，`total_votes` 不变
- 重复选择同一项 → 不重复计数，数据不变

### 3.3 结束投票 — `POST /polls/{poll_id}/close`

- 将 `status` 改为 `closed`；
- **重复结票幂等**：直接返回相同结果；
- 结票后拒绝一切投票 / 改票请求。

### 3.4 重置 — `POST /reset`

- 清空所有投票、用户选择与 id 计数；
- 返回 `{"ok": true}`；
- 重置后下一次创建仍从 `poll_1` 开始。

---

## 4. 业务规则

### 4.1 通用

- 参数非法、投票不存在、状态不允许 → **拒绝操作**；
- 拒绝时**不得改变数据**，**不得消耗新 id**；
- 具体 HTTP 错误码不做强制要求，响应使用 JSON 即可；
- 写操作与 `reset` 需**原子性**，可处理判题器连续请求。

### 4.2 状态枚举（精确字符串）

| 字段 | 取值 |
| --- | --- |
| `Poll.status` | `open` / `closed` |

### 4.3 投票与结票

- 每名用户在一场投票中**只保留一张有效票**；
- `total_votes` 始终等于有效投票用户数；
- 只有 `open` 状态可接收投票 / 改票；
- 结票后数据不再变化。

---

## 5. 边界与校验汇总

| 字段 | 规则 |
| --- | --- |
| `title` | 必填；去除首尾空白后长度 **1~80** |
| `options` | 数组长度 **2~8** |
| 单个候选项 | 去除首尾空白后长度 **1~40**，且不得重复（去空白后比较） |
| `voter_id` | 仅允许 `[A-Za-z0-9_-]`，长度 **1~32** |
| `poll_id` | 必须存在，否则拒绝 |
| `option`（投票） | 必须是该投票已创建的候选项之一 |
| `status`（投票时） | 必须为 `open` |

---

## 6. 示例流程

```http
# 1) 创建投票
POST /polls
{"title":"午餐选择","options":["米饭","面条"]}
→ id=poll_1, status=open, results=[米饭:0, 面条:0], total_votes=0

# 2) u_1 首次投票
PUT /polls/poll_1/votes/u_1
{"option":"米饭"}
→ 米饭=1, 面条=0, total_votes=1

# 3) u_1 改票
PUT /polls/poll_1/votes/u_1
{"option":"面条"}
→ 面条=1, 米饭=0, total_votes=1（按票数重排）

# 4) u_1 重复选同一项
PUT /polls/poll_1/votes/u_1
{"option":"面条"}
→ 数据不变，total_votes 仍为 1

# 5) 结票
POST /polls/poll_1/close
→ status=closed

# 6) 结票后投票 → 拒绝，数据不变
PUT /polls/poll_1/votes/u_2
{"option":"米饭"}

# 7) 重复结票 → 返回相同结果
POST /polls/poll_1/close
```

> 并列票数时按创建顺序排列：`["米饭","面条"]` 在票数都为 0 或都为 1 时保持该顺序输出。

---

## 7. 本地自测

```bash
PORT=8000 python3 main.py &
python3 sample_testcases/run_sample_testcases.py
```

- 公开样例仅校验**连通性**与**部分基础契约**；
- 通过全部样例**不代表**最终得分。

### 隐藏用例常见覆盖点

- `title` 长度 1 / 80 / 81，空白字符边界；
- `options` 数量 2 / 8 / 9；
- 候选项去空白后重复；
- `voter_id` 长度 1 / 32 / 33，非法字符；
- 首次投票、改票、重复投票；
- 结票幂等、结票后拒绝投票；
- `reset` 后 id 重新从 `poll_1` 开始；
- 并列票数排序。

---

## 8. 实现要点

1. **单一数据源**
   以 `{poll_id: {title, status, option_order, votes_map, voter_choice}}` 维护状态，`results`、`total_votes`、用户选择三者从同一数据推导，避免冗余计数漂移。

2. **原子写**
   对单次请求内的「校验 + 变更」加锁（或使用单线程事件循环），防止并发下重复计数。

3. **id 只在创建成功时递增**
   非法请求不得消耗 id。

4. **输出时重新排序 `results`**
   按「票数降序 → 原始顺序」显式排序，不依赖字典顺序。

5. **结票幂等**
   `close` 时若已 `closed`，直接返回当前快照。

6. **参数规范化**
   - `title` / 每个 `option` 先 `strip()` 再判长度与去重；
   - `voter_id` 用正则 `^[A-Za-z0-9_-]{1,32}$` 校验。

7. **错误响应**
   统一返回 JSON（如 `{"error": "..."}`），非 2xx 即可，重点是不改数据。

8. **reset 即全量清空**
   包括 poll 计数器、所有投票与用户选择，保证启动状态与 reset 后状态一致。

---

## 附：开发环境

可用环境：Python 3.11、Go 1.22、Java 21、Node.js 22 + tsx、g++ 12（C++17）。本题使用**标准库**即可完成。