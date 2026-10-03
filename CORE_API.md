# FileManager 核心接口说明

## 1. 模块边界

`FileNode / FileSystem / Operation / OperationStack` 属于核心层。

GUI、搜索、导航、撤销、持久化等模块通过公共接口访问核心，不应直接修改 `FileNode` 的 private 成员，也不要自行 `new/delete FileNode`。

依赖方向保持：

```text
GUI
 ↓
业务模块
 ↓
FileSystem
 ↓
FileNode
```

核心代码不依赖 Qt GUI。

## 2. 树结构

使用孩子-兄弟表示法：

- `parent`：父节点
- `first_child`：第一个子节点
- `next_sibling`：同级下一个节点

根节点名称为 `root`，根路径固定为 `/`。

## 3. 名称规则

当前实现要求节点名：

- 不能为空；
- 不能是 `.` 或 `..`；
- 不能包含 `/`；
- 不能包含 `\\`；
- 不能包含 `|`；
- 不能包含换行符 `\n` 或 `\r`；
- 同一目录下不能重名。

`|` 和换行限制是为了保证后续自定义文本持久化格式可以稳定解析。

## 4. 路径规则

- `/`：根目录
- `/docs/a.txt`：绝对路径
- `docs/a.txt`：相对当前目录路径
- `.`：当前目录
- `..`：父目录；根目录继续停留在根目录

活动树中的路径通过 `get_path()` 根据 `parent` 动态计算，因此节点移动后路径自动变化。

## 5. 删除与回收站

`delete_node()`：从活动树摘除，进入回收站，不立即释放节点。

`restore_node()`：将回收站节点重新挂入指定活动目录。

`permanent_delete()`：仅接受回收站中的节点，递归释放整个子树。

`clear_recycle_bin()`：永久释放回收站中的全部节点。

永久删除后的 `FileNode*` 立即失效，不得继续保存到 undo 栈或其他长期引用中。

## 6. 文件内容

只有普通文件具有可检索的 `content`。

`set_content()` 会同步更新：

- `size`
- `modified_time`

内容检索模块可以使用：

```cpp
file_system.traverse(start_node, [](FileNode* node) {
    // node->get_name()
    // node->get_content()
});
```

## 7. 属性与持久化

`set_metadata()` 用于持久化模块恢复节点属性：

```cpp
set_metadata(node, size, created_time, modified_time);
```

`size < 0` 会失败。

## 7.1 复制语义

`copy_node()` 创建与源节点完全独立的新节点；复制文件夹时递归复制整棵子树。

时间属性统一约定：

- `created_time` 使用复制发生的当前时间；
- `modified_time` 继承源节点的修改时间；
- `size` 与普通文件的 `content` 从源节点复制。

源节点及其原有子树不得因为复制而发生变化。

## 8. 深度优先遍历

`traverse(start, visitor)` 使用 DFS，包含起始节点。

搜索、分类、统计等模块不需要知道孩子-兄弟指针的具体组织方式。

## 9. Operation / OperationStack

`Operation` 保存撤销所需信息，使用非拥有 `FileNode*` / `FileNode* parent` 指针。

`OperationStack` 提供：

- `push`
- `pop`
- `peek`
- `empty`
- `size`
- `clear`

推荐撤销语义：

- CREATE -> 删除刚创建的节点
- DELETE -> 从回收站恢复
- RENAME -> 使用 `old_name` 改回去
- MOVE -> 使用 `old_parent` 移回去
- COPY -> 删除复制出的节点
- RESTORE -> 再放回回收站

**注意：** 永久删除或清空回收站后，撤销模块必须主动丢弃涉及已释放节点的历史记录，避免悬空指针。

## 10. 公共接口规则

以下接口属于公共契约，双方不要私自改名、改参数或改返回值。

如果必须新增或修改核心接口：

1. 先沟通；
2. 修改 `FileSystem.h` / `Operation.h`；
3. 同步更新本文件；
4. 在 Git 提交信息中明确说明接口变化。

## 11. 建议的模块分工

### 核心负责人

- FileNode
- FileSystem
- 树与链表
- 创建 / 删除 / 恢复 / 永久删除
- 重命名 / 移动 / 复制
- 回收站底层逻辑
- 核心接口

### 业务 / GUI 负责人

- SearchManager
- NavigationManager
- PersistenceManager
- UndoManager
- Qt GUI

可直接使用：

- `get_root()`
- `get_current_directory()`
- `change_directory()`
- `change_directory_by_path()`
- `find_child()`
- `find_by_path()`
- `traverse()`
- `get_path()`
- `get_content()`
- `set_content()`
- `set_metadata()`
- `get_recycle_count()`
- `for_each_recycle_item()`
- `restore_node()`
- `permanent_delete()`
- `OperationStack`
