#include "Operation.h"

Operation::Operation()
    : type(OperationType::CREATE),
      node(nullptr),
      old_name(),
      new_name(),
      old_parent(nullptr),
      new_parent(nullptr),
      old_path(),
      new_path()
{
}

OperationStack::OperationStack()
    : top_(nullptr),
      size_(0)
{
}

OperationStack::~OperationStack()
{
    clear();
}

void OperationStack::push(const Operation& operation)
{
    top_ = new Node(operation, top_);
    ++size_;
}

bool OperationStack::pop(Operation& operation)
{
    if (top_ == nullptr)
    {
        return false;
    }

    Node* node = top_;
    operation = node->operation;
    top_ = node->next;

    delete node;
    --size_;
    return true;
}

bool OperationStack::peek(Operation& operation) const
{
    if (top_ == nullptr)
    {
        return false;
    }

    operation = top_->operation;
    return true;
}

bool OperationStack::empty() const
{
    return top_ == nullptr;
}

std::size_t OperationStack::size() const
{
    return size_;
}

void OperationStack::clear()
{
    while (top_ != nullptr)
    {
        Node* node = top_;
        top_ = top_->next;
        delete node;
    }

    size_ = 0;
}
