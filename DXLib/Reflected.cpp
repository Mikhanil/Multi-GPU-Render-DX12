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

void Reflected::Draw(ID3D12GraphicsCommandList* cmdList)
{
	originRenderer->Draw(cmdList);
}
