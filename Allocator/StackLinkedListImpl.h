#include "StackLinkedList.h"
#include "pch.h"

namespace PEPEngine::Allocator
{
    template <class T>
    void StackLinkedList<T>::push(Node* newNode)
    {
        newNode->next = head;
        head = newNode;
    }

    template <class T>
    typename StackLinkedList<T>::Node* StackLinkedList<T>::pop()
    {
        Node* top = head;
        head = head->next;
        return top;
    }
}
