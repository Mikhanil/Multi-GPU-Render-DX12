#pragma once

#include "pch.h"

namespace PEPEngine::Allocator
{
    template <class T>
    class StackLinkedList
    {
    public:
        struct Node
        {
            T data;
            Node* next;
        };

        Node* head;
        StackLinkedList() = default;
        StackLinkedList(StackLinkedList& stackLinkedList) = delete;
        void push(Node* newNode);
        Node* pop();
    };
}
