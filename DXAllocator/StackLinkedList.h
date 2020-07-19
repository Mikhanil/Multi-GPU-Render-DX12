#pragma once

#include "pch.h"
template <class T>
class StackLinkedList {
public:
    struct Node {
        T data;
        Node* next;
    };
    
    Node* head;
public:
    StackLinkedList() = default;
    StackLinkedList(StackLinkedList &stackLinkedList) = delete;
    void push(Node * newNode);
    Node* pop();
};

