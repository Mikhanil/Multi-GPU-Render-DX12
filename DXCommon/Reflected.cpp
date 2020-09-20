#include "pch.h"
#include "Reflected.h"
#include "GameObject.h"

Reflected::Reflected(Renderer* originR): originRenderer(originR)
{
}

void Reflected::Update()
{
	const Matrix reflect = Matrix::CreateReflection(mirrorPlane);
	gameObject->GetTransform()->SetWorldMatrix(originRenderer->gameObject->GetTransform()->GetWorldMatrix() * reflect);
}

void Reflected::Draw(std::shared_ptr<GCommandList> cmdList)
{
	originRenderer->Draw(cmdList);
}
