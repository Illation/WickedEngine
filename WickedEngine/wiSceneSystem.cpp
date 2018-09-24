#include "wiSceneSystem.h"
#include "wiMath.h"
#include "wiTextureHelper.h"
#include "wiResourceManager.h"
#include "wiPhysicsEngine.h"
#include "wiArchive.h"

using namespace wiECS;
using namespace wiGraphicsTypes;

namespace wiSceneSystem
{

	XMFLOAT3 TransformComponent::GetPosition() const
	{
		return *((XMFLOAT3*)&world._41);
	}
	XMFLOAT4 TransformComponent::GetRotation() const
	{
		XMFLOAT4 rotation;
		XMStoreFloat4(&rotation, GetRotationV());
		return rotation;
	}
	XMFLOAT3 TransformComponent::GetScale() const
	{
		XMFLOAT3 scale;
		XMStoreFloat3(&scale, GetScaleV());
		return scale;
	}
	XMVECTOR TransformComponent::GetPositionV() const
	{
		return XMLoadFloat3((XMFLOAT3*)&world._41);
	}
	XMVECTOR TransformComponent::GetRotationV() const
	{
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world));
		return R;
	}
	XMVECTOR TransformComponent::GetScaleV() const
	{
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world));
		return S;
	}
	void TransformComponent::UpdateTransform()
	{
		if (IsDirty())
		{
			SetDirty(false);

			XMVECTOR S_local = XMLoadFloat3(&scale_local);
			XMVECTOR R_local = XMLoadFloat4(&rotation_local);
			XMVECTOR T_local = XMLoadFloat3(&translation_local);
			XMMATRIX W =
				XMMatrixScalingFromVector(S_local) *
				XMMatrixRotationQuaternion(R_local) *
				XMMatrixTranslationFromVector(T_local);

			XMStoreFloat4x4(&world, W);
		}
	}
	void TransformComponent::UpdateParentedTransform(const TransformComponent& parent, const XMFLOAT4X4& inverseParentBindMatrix)
	{
		XMMATRIX W;

		// Normally, every transform would be NOT dirty at this point, but...

		if (parent.IsDirty())
		{
			// If parent is dirty, that means parent ws updated for some reason (anim system, physics or user...)
			//	So we need to propagate the new parent matrix down to this child
			SetDirty();

			W = XMLoadFloat4x4(&world);
		}
		else
		{
			// If it is not dirty, then we still need to propagate parent's matrix to this, 
			//	because every transform is marked as NOT dirty at the end of transform update system
			//	but we look up the local matrix instead, because world matrix might contain 
			//	results from previous run of the hierarchy system...
			XMVECTOR S_local = XMLoadFloat3(&scale_local);
			XMVECTOR R_local = XMLoadFloat4(&rotation_local);
			XMVECTOR T_local = XMLoadFloat3(&translation_local);
			W = XMMatrixScalingFromVector(S_local) *
				XMMatrixRotationQuaternion(R_local) *
				XMMatrixTranslationFromVector(T_local);
		}

		XMMATRIX W_parent = XMLoadFloat4x4(&parent.world);
		XMMATRIX B = XMLoadFloat4x4(&inverseParentBindMatrix);
		W = W * B * W_parent;

		XMStoreFloat4x4(&world, W);
	}
	void TransformComponent::ApplyTransform()
	{
		SetDirty();

		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world));
		XMStoreFloat3(&scale_local, S);
		XMStoreFloat4(&rotation_local, R);
		XMStoreFloat3(&translation_local, T);
	}
	void TransformComponent::ClearTransform()
	{
		SetDirty();
		scale_local = XMFLOAT3(1, 1, 1);
		rotation_local = XMFLOAT4(0, 0, 0, 1);
		translation_local = XMFLOAT3(0, 0, 0);
	}
	void TransformComponent::Translate(const XMFLOAT3& value)
	{
		SetDirty();
		translation_local.x += value.x;
		translation_local.y += value.y;
		translation_local.z += value.z;
	}
	void TransformComponent::RotateRollPitchYaw(const XMFLOAT3& value)
	{
		SetDirty();

		// This needs to be handled a bit differently
		XMVECTOR quat = XMLoadFloat4(&rotation_local);
		XMVECTOR x = XMQuaternionRotationRollPitchYaw(value.x, 0, 0);
		XMVECTOR y = XMQuaternionRotationRollPitchYaw(0, value.y, 0);
		XMVECTOR z = XMQuaternionRotationRollPitchYaw(0, 0, value.z);

		quat = XMQuaternionMultiply(x, quat);
		quat = XMQuaternionMultiply(quat, y);
		quat = XMQuaternionMultiply(z, quat);
		quat = XMQuaternionNormalize(quat);

		XMStoreFloat4(&rotation_local, quat);
	}
	void TransformComponent::Rotate(const XMFLOAT4& quaternion)
	{
		SetDirty();

		XMVECTOR result = XMQuaternionMultiply(XMLoadFloat4(&rotation_local), XMLoadFloat4(&quaternion));
		result = XMQuaternionNormalize(result);
		XMStoreFloat4(&rotation_local, result);
	}
	void TransformComponent::Scale(const XMFLOAT3& value)
	{
		SetDirty();
		scale_local.x *= value.x;
		scale_local.y *= value.y;
		scale_local.z *= value.z;
	}
	void TransformComponent::MatrixTransform(const XMFLOAT4X4& matrix)
	{
		MatrixTransform(XMLoadFloat4x4(&matrix));
	}
	void TransformComponent::MatrixTransform(const XMMATRIX& matrix)
	{
		SetDirty();

		XMVECTOR S;
		XMVECTOR R;
		XMVECTOR T;
		XMMatrixDecompose(&S, &R, &T, matrix);

		S = XMLoadFloat3(&scale_local) * S;
		R = XMQuaternionMultiply(XMLoadFloat4(&rotation_local), R);
		T = XMLoadFloat3(&translation_local) + T;

		XMStoreFloat3(&scale_local, S);
		XMStoreFloat4(&rotation_local, R);
		XMStoreFloat3(&translation_local, T);
	}
	void TransformComponent::Lerp(const TransformComponent& a, const TransformComponent& b, float t)
	{
		SetDirty();

		XMVECTOR aS, aR, aT;
		XMMatrixDecompose(&aS, &aR, &aT, XMLoadFloat4x4(&a.world));

		XMVECTOR bS, bR, bT;
		XMMatrixDecompose(&bS, &bR, &bT, XMLoadFloat4x4(&b.world));

		XMVECTOR S = XMVectorLerp(aS, bS, t);
		XMVECTOR R = XMQuaternionSlerp(aR, bR, t);
		XMVECTOR T = XMVectorLerp(aT, bT, t);

		XMStoreFloat3(&scale_local, S);
		XMStoreFloat4(&rotation_local, R);
		XMStoreFloat3(&translation_local, T);
	}
	void TransformComponent::CatmullRom(const TransformComponent& a, const TransformComponent& b, const TransformComponent& c, const TransformComponent& d, float t)
	{
		SetDirty();

		XMVECTOR aS, aR, aT;
		XMMatrixDecompose(&aS, &aR, &aT, XMLoadFloat4x4(&a.world));

		XMVECTOR bS, bR, bT;
		XMMatrixDecompose(&bS, &bR, &bT, XMLoadFloat4x4(&b.world));

		XMVECTOR cS, cR, cT;
		XMMatrixDecompose(&cS, &cR, &cT, XMLoadFloat4x4(&c.world));

		XMVECTOR dS, dR, dT;
		XMMatrixDecompose(&dS, &dR, &dT, XMLoadFloat4x4(&d.world));

		XMVECTOR T = XMVectorCatmullRom(aT, bT, cT, dT, t);

		// Catmull-rom has issues with full rotation for quaternions (todo):
		XMVECTOR R = XMVectorCatmullRom(aR, bR, cR, dR, t);
		R = XMQuaternionNormalize(R);

		XMVECTOR S = XMVectorCatmullRom(aS, bS, cS, dS, t);

		XMStoreFloat3(&translation_local, T);
		XMStoreFloat4(&rotation_local, R);
		XMStoreFloat3(&scale_local, S);
	}

	Texture2D* MaterialComponent::GetBaseColorMap() const
	{
		if (baseColorMap != nullptr)
		{
			return baseColorMap;
		}
		return wiTextureHelper::getInstance()->getWhite();
	}
	Texture2D* MaterialComponent::GetNormalMap() const
	{
		return normalMap;
		//if (normalMap != nullptr)
		//{
		//	return normalMap;
		//}
		//return wiTextureHelper::getInstance()->getNormalMapDefault();
	}
	Texture2D* MaterialComponent::GetSurfaceMap() const
	{
		if (surfaceMap != nullptr)
		{
			return surfaceMap;
		}
		return wiTextureHelper::getInstance()->getWhite();
	}
	Texture2D* MaterialComponent::GetDisplacementMap() const
	{
		if (displacementMap != nullptr)
		{
			return displacementMap;
		}
		return wiTextureHelper::getInstance()->getWhite();
	}

	void MeshComponent::CreateRenderData()
	{
		HRESULT hr;

		// Create index buffer GPU data:
		{
			uint32_t counter = 0;
			uint8_t stride;
			void* gpuIndexData;
			if (GetIndexFormat() == INDEXFORMAT_32BIT)
			{
				gpuIndexData = new uint32_t[indices.size()];
				stride = sizeof(uint32_t);

				for (auto& x : indices)
				{
					static_cast<uint32_t*>(gpuIndexData)[counter++] = static_cast<uint32_t>(x);
				}

			}
			else
			{
				gpuIndexData = new uint16_t[indices.size()];
				stride = sizeof(uint16_t);

				for (auto& x : indices)
				{
					static_cast<uint16_t*>(gpuIndexData)[counter++] = static_cast<uint16_t>(x);
				}

			}


			GPUBufferDesc bd;
			bd.Usage = USAGE_IMMUTABLE;
			bd.CPUAccessFlags = 0;
			bd.BindFlags = BIND_INDEX_BUFFER | BIND_SHADER_RESOURCE;
			bd.MiscFlags = 0;
			bd.StructureByteStride = stride;
			bd.Format = GetIndexFormat() == INDEXFORMAT_16BIT ? FORMAT_R16_UINT : FORMAT_R32_UINT;

			SubresourceData InitData;
			InitData.pSysMem = gpuIndexData;
			bd.ByteWidth = (UINT)(stride * indices.size());
			indexBuffer.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, indexBuffer.get());
			assert(SUCCEEDED(hr));

			SAFE_DELETE_ARRAY(gpuIndexData);
		}


		XMFLOAT3 _min = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
		XMFLOAT3 _max = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		// vertexBuffer - POSITION + NORMAL + SUBSETINDEX:
		{
			std::vector<uint8_t> vertex_subsetindices(vertex_positions.size());

			uint32_t subsetCounter = 0;
			for (auto& subset : subsets)
			{
				for (uint32_t i = 0; i < subset.indexCount; ++i)
				{
					uint32_t index = indices[subset.indexOffset + i];
					vertex_subsetindices[index] = subsetCounter;
				}
				subsetCounter++;
			}

			std::vector<Vertex_POS> vertices(vertex_positions.size());
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				const XMFLOAT3& pos = vertex_positions[i];
				XMFLOAT3& nor = vertex_normals.empty() ? XMFLOAT3(1, 1, 1) : vertex_normals[i];
				XMStoreFloat3(&nor, XMVector3Normalize(XMLoadFloat3(&nor)));
				uint32_t subsetIndex = vertex_subsetindices[i];
				vertices[i].FromFULL(pos, nor, subsetIndex);

				_min = wiMath::Min(_min, pos);
				_max = wiMath::Max(_max, pos);
			}

			GPUBufferDesc bd;
			bd.Usage = USAGE_IMMUTABLE;
			bd.CPUAccessFlags = 0;
			bd.BindFlags = BIND_VERTEX_BUFFER | BIND_SHADER_RESOURCE;
			bd.MiscFlags = RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			bd.ByteWidth = (UINT)(sizeof(Vertex_POS) * vertices.size());

			SubresourceData InitData;
			InitData.pSysMem = vertices.data();
			vertexBuffer_POS.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, vertexBuffer_POS.get());
			assert(SUCCEEDED(hr));
		}

		aabb = AABB(_min, _max);

		// skinning buffers:
		if (!vertex_boneindices.empty())
		{
			std::vector<Vertex_BON> vertices(vertex_boneindices.size());
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				XMFLOAT4& wei = vertex_boneweights[i];
				// normalize bone weights
				float len = wei.x + wei.y + wei.z + wei.w;
				if (len > 0)
				{
					wei.x /= len;
					wei.y /= len;
					wei.z /= len;
					wei.w /= len;
				}
				vertices[i].FromFULL(vertex_boneindices[i], wei);
			}

			GPUBufferDesc bd;
			bd.Usage = USAGE_IMMUTABLE;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.CPUAccessFlags = 0;
			bd.MiscFlags = RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			bd.ByteWidth = (UINT)(sizeof(Vertex_BON) * vertices.size());

			SubresourceData InitData;
			InitData.pSysMem = vertices.data();
			vertexBuffer_BON.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, vertexBuffer_BON.get());
			assert(SUCCEEDED(hr));

			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_VERTEX_BUFFER | BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;
			bd.CPUAccessFlags = 0;
			bd.MiscFlags = RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

			bd.ByteWidth = (UINT)(sizeof(Vertex_POS) * vertex_positions.size());
			streamoutBuffer_POS.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, nullptr, streamoutBuffer_POS.get());
			assert(SUCCEEDED(hr));

			bd.ByteWidth = (UINT)(sizeof(Vertex_POS) * vertex_positions.size());
			streamoutBuffer_PRE.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, nullptr, streamoutBuffer_PRE.get());
			assert(SUCCEEDED(hr));
		}

		// vertexBuffer - TEXCOORDS
		if(!vertex_texcoords.empty())
		{
			std::vector<Vertex_TEX> vertices(vertex_texcoords.size());
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				vertices[i].FromFULL(vertex_texcoords[i]);
			}

			GPUBufferDesc bd;
			bd.Usage = USAGE_IMMUTABLE;
			bd.CPUAccessFlags = 0;
			bd.BindFlags = BIND_VERTEX_BUFFER | BIND_SHADER_RESOURCE;
			bd.MiscFlags = 0;
			bd.StructureByteStride = sizeof(Vertex_TEX);
			bd.ByteWidth = (UINT)(bd.StructureByteStride * vertices.size());
			bd.Format = Vertex_TEX::FORMAT;

			SubresourceData InitData;
			InitData.pSysMem = vertices.data();
			vertexBuffer_TEX.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, vertexBuffer_TEX.get());
			assert(SUCCEEDED(hr));
		}

		// vertexBuffer - COLORS
		if (!vertex_colors.empty())
		{
			GPUBufferDesc bd;
			bd.Usage = USAGE_IMMUTABLE;
			bd.CPUAccessFlags = 0;
			bd.BindFlags = BIND_VERTEX_BUFFER | BIND_SHADER_RESOURCE;
			bd.MiscFlags = 0;
			bd.StructureByteStride = sizeof(uint32_t);
			bd.ByteWidth = (UINT)(bd.StructureByteStride * vertex_colors.size());
			bd.Format = FORMAT_R8G8B8A8_UNORM;

			SubresourceData InitData;
			InitData.pSysMem = vertex_colors.data();
			vertexBuffer_COL.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, vertexBuffer_COL.get());
			assert(SUCCEEDED(hr));
		}

		// vertexBuffer - ATLAS
		if (!vertex_atlas.empty())
		{
			std::vector<Vertex_TEX> vertices(vertex_atlas.size());
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				vertices[i].FromFULL(vertex_atlas[i]);
			}

			GPUBufferDesc bd;
			bd.Usage = USAGE_IMMUTABLE;
			bd.CPUAccessFlags = 0;
			bd.BindFlags = BIND_VERTEX_BUFFER | BIND_SHADER_RESOURCE;
			bd.MiscFlags = 0;
			bd.StructureByteStride = sizeof(Vertex_TEX);
			bd.ByteWidth = (UINT)(bd.StructureByteStride * vertices.size());
			bd.Format = Vertex_TEX::FORMAT;

			SubresourceData InitData;
			InitData.pSysMem = vertices.data();
			vertexBuffer_ATL.reset(new GPUBuffer);
			hr = wiRenderer::GetDevice()->CreateBuffer(&bd, &InitData, vertexBuffer_ATL.get());
			assert(SUCCEEDED(hr));
		}

	}
	void MeshComponent::ComputeNormals(bool smooth)
	{
		// Start recalculating normals:

		if (smooth)
		{
			// Compute smooth surface normals:

			// 1.) Zero normals, they will be averaged later
			for (size_t i = 0; i < vertex_normals.size(); i++)
			{
				vertex_normals[i] = XMFLOAT3(0, 0, 0);
			}

			// 2.) Find identical vertices by POSITION, accumulate face normals
			for (size_t i = 0; i < vertex_positions.size(); i++)
			{
				XMFLOAT3& v_search_pos = vertex_positions[i];

				for (size_t ind = 0; ind < indices.size() / 3; ++ind)
				{
					uint32_t i0 = indices[ind * 3 + 0];
					uint32_t i1 = indices[ind * 3 + 1];
					uint32_t i2 = indices[ind * 3 + 2];

					XMFLOAT3& v0 = vertex_positions[i0];
					XMFLOAT3& v1 = vertex_positions[i1];
					XMFLOAT3& v2 = vertex_positions[i2];

					bool match_pos0 =
						fabs(v_search_pos.x - v0.x) < FLT_EPSILON &&
						fabs(v_search_pos.y - v0.y) < FLT_EPSILON &&
						fabs(v_search_pos.z - v0.z) < FLT_EPSILON;

					bool match_pos1 =
						fabs(v_search_pos.x - v1.x) < FLT_EPSILON &&
						fabs(v_search_pos.y - v1.y) < FLT_EPSILON &&
						fabs(v_search_pos.z - v1.z) < FLT_EPSILON;

					bool match_pos2 =
						fabs(v_search_pos.x - v2.x) < FLT_EPSILON &&
						fabs(v_search_pos.y - v2.y) < FLT_EPSILON &&
						fabs(v_search_pos.z - v2.z) < FLT_EPSILON;

					if (match_pos0 || match_pos1 || match_pos2)
					{
						XMVECTOR U = XMLoadFloat3(&v2) - XMLoadFloat3(&v0);
						XMVECTOR V = XMLoadFloat3(&v1) - XMLoadFloat3(&v0);

						XMVECTOR N = XMVector3Cross(U, V);
						N = XMVector3Normalize(N);

						XMFLOAT3 normal;
						XMStoreFloat3(&normal, N);

						vertex_normals[i].x += normal.x;
						vertex_normals[i].y += normal.y;
						vertex_normals[i].z += normal.z;
					}

				}
			}

			// 3.) Find duplicated vertices by POSITION and TEXCOORD and SUBSET and remove them:
			for (auto& subset : subsets)
			{
				for (uint32_t i = 0; i < subset.indexCount - 1; i++)
				{
					uint32_t ind0 = indices[subset.indexOffset + (uint32_t)i];
					const XMFLOAT3& p0 = vertex_positions[ind0];
					const XMFLOAT3& n0 = vertex_normals[ind0];
					const XMFLOAT2& t0 = vertex_texcoords[ind0];

					for (uint32_t j = i + 1; j < subset.indexCount; j++)
					{
						uint32_t ind1 = indices[subset.indexOffset + (uint32_t)j];

						if (ind1 == ind0)
						{
							continue;
						}

						const XMFLOAT3& p1 = vertex_positions[ind1];
						const XMFLOAT3& n1 = vertex_normals[ind1];
						const XMFLOAT2& t1 = vertex_texcoords[ind1];

						bool duplicated_pos =
							fabs(p0.x - p1.x) < FLT_EPSILON &&
							fabs(p0.y - p1.y) < FLT_EPSILON &&
							fabs(p0.z - p1.z) < FLT_EPSILON;

						bool duplicated_tex =
							fabs(t0.x - t1.x) < FLT_EPSILON &&
							fabs(t0.y - t1.y) < FLT_EPSILON;

						if (duplicated_pos && duplicated_tex)
						{
							// Erase vertices[ind1] because it is a duplicate:
							if (ind1 < vertex_positions.size())
							{
								vertex_positions.erase(vertex_positions.begin() + ind1);
							}
							if (ind1 < vertex_normals.size())
							{
								vertex_normals.erase(vertex_normals.begin() + ind1);
							}
							if (ind1 < vertex_texcoords.size())
							{
								vertex_texcoords.erase(vertex_texcoords.begin() + ind1);
							}
							if (ind1 < vertex_boneindices.size())
							{
								vertex_boneindices.erase(vertex_boneindices.begin() + ind1);
							}
							if (ind1 < vertex_boneweights.size())
							{
								vertex_boneweights.erase(vertex_boneweights.begin() + ind1);
							}

							// The vertices[ind1] was removed, so each index after that needs to be updated:
							for (auto& index : indices)
							{
								if (index > ind1 && index > 0)
								{
									index--;
								}
								else if (index == ind1)
								{
									index = ind0;
								}
							}

						}

					}
				}

			}

		}
		else
		{
			// Compute hard surface normals:

			std::vector<uint32_t> newIndexBuffer;
			std::vector<XMFLOAT3> newPositionsBuffer;
			std::vector<XMFLOAT3> newNormalsBuffer;
			std::vector<XMFLOAT2> newTexcoordsBuffer;
			std::vector<XMUINT4> newBoneIndicesBuffer;
			std::vector<XMFLOAT4> newBoneWeightsBuffer;

			for (size_t face = 0; face < indices.size() / 3; face++)
			{
				uint32_t i0 = indices[face * 3 + 0];
				uint32_t i1 = indices[face * 3 + 1];
				uint32_t i2 = indices[face * 3 + 2];

				XMFLOAT3& p0 = vertex_positions[i0];
				XMFLOAT3& p1 = vertex_positions[i1];
				XMFLOAT3& p2 = vertex_positions[i2];

				XMVECTOR U = XMLoadFloat3(&p2) - XMLoadFloat3(&p0);
				XMVECTOR V = XMLoadFloat3(&p1) - XMLoadFloat3(&p0);

				XMVECTOR N = XMVector3Cross(U, V);
				N = XMVector3Normalize(N);

				XMFLOAT3 normal;
				XMStoreFloat3(&normal, N);

				newPositionsBuffer.push_back(p0);
				newPositionsBuffer.push_back(p1);
				newPositionsBuffer.push_back(p2);

				newNormalsBuffer.push_back(normal);
				newNormalsBuffer.push_back(normal);
				newNormalsBuffer.push_back(normal);

				newTexcoordsBuffer.push_back(vertex_texcoords[i0]);
				newTexcoordsBuffer.push_back(vertex_texcoords[i1]);
				newTexcoordsBuffer.push_back(vertex_texcoords[i2]);

				if (!vertex_boneindices.empty())
				{
					newBoneIndicesBuffer.push_back(vertex_boneindices[i0]);
					newBoneIndicesBuffer.push_back(vertex_boneindices[i1]);
					newBoneIndicesBuffer.push_back(vertex_boneindices[i2]);
				}

				if (!vertex_boneweights.empty())
				{
					newBoneWeightsBuffer.push_back(vertex_boneweights[i0]);
					newBoneWeightsBuffer.push_back(vertex_boneweights[i1]);
					newBoneWeightsBuffer.push_back(vertex_boneweights[i2]);
				}

				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
				newIndexBuffer.push_back(static_cast<uint32_t>(newIndexBuffer.size()));
			}

			// For hard surface normals, we created a new mesh in the previous loop through faces, so swap data:
			vertex_positions = newPositionsBuffer;
			vertex_normals = newNormalsBuffer;
			vertex_texcoords = newTexcoordsBuffer;
			if (!vertex_boneindices.empty())
			{
				vertex_boneindices = newBoneIndicesBuffer;
			}
			if (!vertex_boneweights.empty())
			{
				vertex_boneweights = newBoneWeightsBuffer;
			}
			indices = newIndexBuffer;
		}

		// Restore subsets:


		CreateRenderData();
	}
	void MeshComponent::FlipCulling()
	{
		for (size_t face = 0; face < indices.size() / 3; face++)
		{
			uint32_t i0 = indices[face * 3 + 0];
			uint32_t i1 = indices[face * 3 + 1];
			uint32_t i2 = indices[face * 3 + 2];

			indices[face * 3 + 0] = i0;
			indices[face * 3 + 1] = i2;
			indices[face * 3 + 2] = i1;
		}

		CreateRenderData();
	}
	void MeshComponent::FlipNormals()
	{
		for (auto& normal : vertex_normals)
		{
			normal.x *= -1;
			normal.y *= -1;
			normal.z *= -1;
		}

		CreateRenderData();
	}
	
	void CameraComponent::CreatePerspective(float newWidth, float newHeight, float newNear, float newFar, float newFOV)
	{
		zNearP = newNear;
		zFarP = newFar;
		width = newWidth;
		height = newHeight;
		fov = newFOV;
		Eye = XMFLOAT3(0, 0, 0);
		At = XMFLOAT3(0, 0, 1);
		Up = XMFLOAT3(0, 1, 0);

		UpdateProjection();
		UpdateCamera();
	}
	void CameraComponent::UpdateProjection()
	{
		XMStoreFloat4x4(&Projection, XMMatrixPerspectiveFovLH(fov, width / height, zFarP, zNearP)); // reverse zbuffer!
		XMStoreFloat4x4(&realProjection, XMMatrixPerspectiveFovLH(fov, width / height, zNearP, zFarP)); // normal zbuffer!
	}
	void CameraComponent::UpdateCamera(const TransformComponent* transform)
	{
		SetDirty(false);

		XMVECTOR _Eye;
		XMVECTOR _At;
		XMVECTOR _Up;

		if (transform != nullptr)
		{
			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&transform->world));

			_Eye = T;
			_At = XMVectorSet(0, 0, 1, 0);
			_Up = XMVectorSet(0, 1, 0, 0);

			XMMATRIX _Rot = XMMatrixRotationQuaternion(R);
			_At = XMVector3TransformNormal(_At, _Rot);
			_Up = XMVector3TransformNormal(_Up, _Rot);
			XMStoreFloat3x3(&rotationMatrix, _Rot);
		}
		else
		{
			_Eye = XMLoadFloat3(&Eye);
			_At = XMLoadFloat3(&At);
			_Up = XMLoadFloat3(&Up);
		}

		XMMATRIX _P = XMLoadFloat4x4(&Projection);
		XMMATRIX _InvP = XMMatrixInverse(nullptr, _P);
		XMStoreFloat4x4(&InvProjection, _InvP);

		XMMATRIX _V = XMMatrixLookToLH(_Eye, _At, _Up);
		XMMATRIX _VP = XMMatrixMultiply(_V, _P);
		XMStoreFloat4x4(&View, _V);
		XMStoreFloat4x4(&VP, _VP);
		XMStoreFloat4x4(&InvView, XMMatrixInverse(nullptr, _V));
		XMStoreFloat4x4(&InvVP, XMMatrixInverse(nullptr, _VP));
		XMStoreFloat4x4(&Projection, _P);
		XMStoreFloat4x4(&InvProjection, XMMatrixInverse(nullptr, _P));

		XMStoreFloat3(&Eye, _Eye);
		XMStoreFloat3(&At, _At);
		XMStoreFloat3(&Up, _Up);

		frustum.ConstructFrustum(zFarP, realProjection, View);
	}
	void CameraComponent::Reflect(const XMFLOAT4& plane)
	{
		XMVECTOR _Eye = XMLoadFloat3(&Eye);
		XMVECTOR _At = XMLoadFloat3(&At);
		XMVECTOR _Up = XMLoadFloat3(&Up);
		XMMATRIX _Ref = XMMatrixReflect(XMLoadFloat4(&plane));

		_Eye = XMVector3Transform(_Eye, _Ref);
		_At = XMVector3TransformNormal(_At, _Ref);
		_Up = XMVector3TransformNormal(_Up, _Ref);

		XMStoreFloat3(&Eye, _Eye);
		XMStoreFloat3(&At, _At);
		XMStoreFloat3(&Up, _Up);

		UpdateCamera();
	}


	void Scene::Update(float dt)
	{
		if (weathers.GetCount() > 0)
		{
			weather = weathers[0];
		}

		RunPreviousFrameTransformUpdateSystem(transforms, prev_transforms);

		RunAnimationUpdateSystem(animations, transforms, dt);

		wiPhysics::RunPhysicsUpdateSystem(weather, transforms, meshes, objects, rigidbodies, softbodies, dt);

		RunTransformUpdateSystem(transforms);

		RunHierarchyUpdateSystem(hierarchy, transforms, layers);

		RunArmatureUpdateSystem(transforms, armatures);

		RunMaterialUpdateSystem(materials, dt);

		RunObjectUpdateSystem(transforms, meshes, materials, objects, aabb_objects, bounds, waterPlane);

		RunCameraUpdateSystem(transforms, cameras);

		RunDecalUpdateSystem(transforms, materials, aabb_decals, decals);

		RunProbeUpdateSystem(transforms, aabb_probes, probes);

		RunForceUpdateSystem(transforms, forces);

		RunLightUpdateSystem(wiRenderer::GetCamera(), transforms, aabb_lights, lights, &weather);

		RunParticleUpdateSystem(transforms, meshes, emitters, hairs, dt);
	}
	void Scene::Clear()
	{
		names.Clear();
		layers.Clear();
		transforms.Clear();
		prev_transforms.Clear();
		hierarchy.Clear();
		materials.Clear();
		meshes.Clear();
		objects.Clear();
		aabb_objects.Clear();
		rigidbodies.Clear();
		softbodies.Clear();
		armatures.Clear();
		lights.Clear();
		aabb_lights.Clear();
		cameras.Clear();
		probes.Clear();
		aabb_probes.Clear();
		forces.Clear();
		decals.Clear();
		aabb_decals.Clear();
		animations.Clear();
		emitters.Clear();
		hairs.Clear();
		weathers.Clear();
	}
	void Scene::Merge(Scene& other)
	{
		names.Merge(other.names);
		layers.Merge(other.layers);
		transforms.Merge(other.transforms);
		prev_transforms.Merge(other.prev_transforms);
		hierarchy.Merge(other.hierarchy);
		materials.Merge(other.materials);
		meshes.Merge(other.meshes);
		objects.Merge(other.objects);
		aabb_objects.Merge(other.aabb_objects);
		rigidbodies.Merge(other.rigidbodies);
		softbodies.Merge(other.softbodies);
		armatures.Merge(other.armatures);
		lights.Merge(other.lights);
		aabb_lights.Merge(other.aabb_lights);
		cameras.Merge(other.cameras);
		probes.Merge(other.probes);
		aabb_probes.Merge(other.aabb_probes);
		forces.Merge(other.forces);
		decals.Merge(other.decals);
		aabb_decals.Merge(other.aabb_decals);
		animations.Merge(other.animations);
		emitters.Merge(other.emitters);
		hairs.Merge(other.hairs);
		weathers.Merge(other.weathers);

		bounds = AABB::Merge(bounds, other.bounds);
	}
	size_t Scene::CountEntities() const
	{
		// Entities are unique within a ComponentManager, so the most populated ComponentManager
		//	will actually give us how many entities there are in the scene
		size_t entityCount = 0;
		entityCount = max(entityCount, names.GetCount());
		entityCount = max(entityCount, layers.GetCount());
		entityCount = max(entityCount, transforms.GetCount());
		entityCount = max(entityCount, prev_transforms.GetCount());
		entityCount = max(entityCount, hierarchy.GetCount());
		entityCount = max(entityCount, materials.GetCount());
		entityCount = max(entityCount, meshes.GetCount());
		entityCount = max(entityCount, objects.GetCount());
		entityCount = max(entityCount, aabb_objects.GetCount());
		entityCount = max(entityCount, rigidbodies.GetCount());
		entityCount = max(entityCount, softbodies.GetCount());
		entityCount = max(entityCount, armatures.GetCount());
		entityCount = max(entityCount, lights.GetCount());
		entityCount = max(entityCount, aabb_lights.GetCount());
		entityCount = max(entityCount, cameras.GetCount());
		entityCount = max(entityCount, probes.GetCount());
		entityCount = max(entityCount, aabb_probes.GetCount());
		entityCount = max(entityCount, forces.GetCount());
		entityCount = max(entityCount, decals.GetCount());
		entityCount = max(entityCount, aabb_decals.GetCount());
		entityCount = max(entityCount, animations.GetCount());
		entityCount = max(entityCount, emitters.GetCount());
		entityCount = max(entityCount, hairs.GetCount());
		return entityCount;
	}

	void Scene::Entity_Remove(Entity entity)
	{
		names.Remove(entity);
		layers.Remove(entity);
		transforms.Remove(entity);
		prev_transforms.Remove(entity);
		hierarchy.Remove_KeepSorted(entity);
		materials.Remove(entity);
		meshes.Remove(entity);
		objects.Remove(entity);
		aabb_objects.Remove(entity);
		rigidbodies.Remove(entity);
		softbodies.Remove(entity);
		armatures.Remove(entity);
		lights.Remove(entity);
		aabb_lights.Remove(entity);
		cameras.Remove(entity);
		probes.Remove(entity);
		aabb_probes.Remove(entity);
		forces.Remove(entity);
		decals.Remove(entity);
		aabb_decals.Remove(entity);
		animations.Remove(entity);
		emitters.Remove(entity);
		hairs.Remove(entity);
		weathers.Remove(entity);
	}
	Entity Scene::Entity_FindByName(const std::string& name)
	{
		for (size_t i = 0; i < names.GetCount(); ++i)
		{
			if (names[i] == name)
			{
				return names.GetEntity(i);
			}
		}
		return INVALID_ENTITY;
	}
	Entity Scene::Entity_Duplicate(Entity entity)
	{
		wiArchive archive;

		// First write the entity to staging area:
		archive.SetReadModeAndResetPos(false);
		Entity_Serialize(archive, entity, 0);

		// Then deserialize with a unique seed:
		archive.SetReadModeAndResetPos(true);
		uint32_t seed = wiRandom::getRandom(1, INT_MAX);
		return Entity_Serialize(archive, entity, seed, false);
	}
	Entity Scene::Entity_CreateMaterial(
		const std::string& name
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		materials.Create(entity);

		return entity;
	}
	Entity Scene::Entity_CreateObject(
		const std::string& name
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		layers.Create(entity);

		transforms.Create(entity);

		prev_transforms.Create(entity);

		aabb_objects.Create(entity);

		objects.Create(entity);

		return entity;
	}
	Entity Scene::Entity_CreateMesh(
		const std::string& name
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		meshes.Create(entity);

		return entity;
	}
	Entity Scene::Entity_CreateLight(
		const std::string& name,
		const XMFLOAT3& position,
		const XMFLOAT3& color,
		float energy,
		float range)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		layers.Create(entity);

		TransformComponent& transform = transforms.Create(entity);
		transform.Translate(position);
		transform.UpdateTransform();

		aabb_lights.Create(entity).createFromHalfWidth(position, XMFLOAT3(range, range, range));

		LightComponent& light = lights.Create(entity);
		light.energy = energy;
		light.range = range;
		light.fov = XM_PIDIV4;
		light.color = color;
		light.SetType(LightComponent::POINT);

		return entity;
	}
	Entity Scene::Entity_CreateForce(
		const std::string& name,
		const XMFLOAT3& position
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		layers.Create(entity);

		TransformComponent& transform = transforms.Create(entity);
		transform.Translate(position);
		transform.UpdateTransform();

		ForceFieldComponent& force = forces.Create(entity);
		force.gravity = 0;
		force.range = 0;
		force.type = ENTITY_TYPE_FORCEFIELD_POINT;

		return entity;
	}
	Entity Scene::Entity_CreateEnvironmentProbe(
		const std::string& name,
		const XMFLOAT3& position
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		layers.Create(entity);

		TransformComponent& transform = transforms.Create(entity);
		transform.Translate(position);
		transform.UpdateTransform();

		aabb_probes.Create(entity);

		probes.Create(entity);

		return entity;
	}
	Entity Scene::Entity_CreateDecal(
		const std::string& name,
		const std::string& textureName,
		const std::string& normalMapName
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		layers.Create(entity);

		transforms.Create(entity);

		aabb_decals.Create(entity);

		decals.Create(entity);

		MaterialComponent& material = materials.Create(entity);

		if (!textureName.empty())
		{
			material.baseColorMapName = textureName;
			material.baseColorMap = (Texture2D*)wiResourceManager::GetGlobal()->add(material.baseColorMapName);
		}
		if (!normalMapName.empty())
		{
			material.normalMapName = normalMapName;
			material.normalMap = (Texture2D*)wiResourceManager::GetGlobal()->add(material.normalMapName);
		}

		return entity;
	}
	Entity Scene::Entity_CreateCamera(
		const std::string& name,
		float width, float height, float nearPlane, float farPlane, float fov
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		layers.Create(entity);

		transforms.Create(entity);

		CameraComponent& camera = cameras.Create(entity);
		camera.CreatePerspective(width, height, nearPlane, farPlane, fov);

		return entity;
	}
	Entity Scene::Entity_CreateEmitter(
		const std::string& name,
		const XMFLOAT3& position
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		emitters.Create(entity);

		TransformComponent& transform = transforms.Create(entity);
		transform.Translate(position);
		transform.UpdateTransform();

		materials.Create(entity).blendFlag = BLENDMODE_ALPHA;

		return entity;
	}
	Entity Scene::Entity_CreateHair(
		const std::string& name,
		const XMFLOAT3& position
	)
	{
		Entity entity = CreateEntity();

		names.Create(entity) = name;

		hairs.Create(entity);

		TransformComponent& transform = transforms.Create(entity);
		transform.Translate(position);
		transform.UpdateTransform();

		materials.Create(entity);

		return entity;
	}

	void Scene::Component_Attach(Entity entity, Entity parent)
	{
		assert(entity != parent);

		if (hierarchy.Contains(entity))
		{
			Component_Detach(entity);
		}

		// Add a new hierarchy node to the end of container:
		hierarchy.Create(entity).parentID = parent;

		// If this entity was already a part of a tree however, we must move it before children:
		for (size_t i = 0; i < hierarchy.GetCount(); ++i)
		{
			const HierarchyComponent& parent = hierarchy[i];
			
			if (parent.parentID == entity)
			{
				hierarchy.MoveLastTo(i);
				break;
			}
		}

		// Re-query parent after potential MoveLastTo(), because it invalidates references:
		HierarchyComponent& parentcomponent = *hierarchy.GetComponent(entity);

		TransformComponent* transform_parent = transforms.GetComponent(parent);
		if (transform_parent != nullptr)
		{
			// Save the parent's inverse worldmatrix:
			XMStoreFloat4x4(&parentcomponent.world_parent_inverse_bind, XMMatrixInverse(nullptr, XMLoadFloat4x4(&transform_parent->world)));

			TransformComponent* transform_child = transforms.GetComponent(entity);
			if (transform_child != nullptr)
			{
				// Child updated immediately, to that it can be immediately attached to afterwards:
				transform_child->UpdateParentedTransform(*transform_parent, parentcomponent.world_parent_inverse_bind);
			}
		}

		LayerComponent* layer_child = layers.GetComponent(entity);
		if (layer_child != nullptr)
		{
			// Save the initial layermask of the child so that it can be restored if detached:
			parentcomponent.layerMask_bind = layer_child->GetLayerMask();
		}
	}
	void Scene::Component_Detach(Entity entity)
	{
		const HierarchyComponent* parent = hierarchy.GetComponent(entity);

		if (parent != nullptr)
		{
			TransformComponent* transform = transforms.GetComponent(entity);
			if (transform != nullptr)
			{
				transform->ApplyTransform();
			}

			LayerComponent* layer = layers.GetComponent(entity);
			if (layer != nullptr)
			{
				layer->layerMask = parent->layerMask_bind;
			}

			hierarchy.Remove_KeepSorted(entity);
		}
	}
	void Scene::Component_DetachChildren(Entity parent)
	{
		for (size_t i = 0; i < hierarchy.GetCount(); )
		{
			if (hierarchy[i].parentID == parent)
			{
				Entity entity = hierarchy.GetEntity(i);
				Component_Detach(entity);
			}
			else
			{
				++i;
			}
		}
	}




	void RunPreviousFrameTransformUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		ComponentManager<PreviousFrameTransformComponent>& prev_transforms
	)
	{
		for (size_t i = 0; i < prev_transforms.GetCount(); ++i)
		{
			PreviousFrameTransformComponent& prev_transform = prev_transforms[i];
			Entity entity = prev_transforms.GetEntity(i);
			const TransformComponent& transform = *transforms.GetComponent(entity);

			prev_transform.world_prev = transform.world;
		}
	}
	void RunAnimationUpdateSystem(
		ComponentManager<AnimationComponent>& animations,
		ComponentManager<TransformComponent>& transforms,
		float dt
	)
	{
		for (size_t i = 0; i < animations.GetCount(); ++i)
		{
			AnimationComponent& animation = animations[i];
			if (!animation.IsPlaying() && animation.timer == 0.0f)
			{
				continue;
			}

			for (const AnimationComponent::AnimationChannel& channel : animation.channels)
			{
				assert(channel.samplerIndex < animation.samplers.size());
				const AnimationComponent::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];

				int keyLeft = 0;
				int keyRight = 0;

				if (sampler.keyframe_times.back() < animation.timer)
				{
					// Rightmost keyframe is already outside animation, so just snap to last keyframe:
					keyLeft = keyRight = (int)sampler.keyframe_times.size() - 1;
				}
				else
				{
					// Search for the right keyframe (greater/equal to anim time):
					while (sampler.keyframe_times[keyRight++] < animation.timer) {}
					keyRight--;

					// Left keyframe is just near right:
					keyLeft = max(0, keyRight - 1);
				}

				float left = sampler.keyframe_times[keyLeft];

				TransformComponent& transform = *transforms.GetComponent(channel.target);

				if (sampler.mode == AnimationComponent::AnimationSampler::Mode::STEP || keyLeft == keyRight)
				{
					// Nearest neighbor method (snap to left):
					switch (channel.path)
					{
					case AnimationComponent::AnimationChannel::Path::TRANSLATION:
					{
						assert(sampler.keyframe_data.size() == sampler.keyframe_times.size() * 3);
						transform.translation_local = ((const XMFLOAT3*)sampler.keyframe_data.data())[keyLeft];
					}
					break;
					case AnimationComponent::AnimationChannel::Path::ROTATION:
					{
						assert(sampler.keyframe_data.size() == sampler.keyframe_times.size() * 4);
						transform.rotation_local = ((const XMFLOAT4*)sampler.keyframe_data.data())[keyLeft];
					}
					break;
					case AnimationComponent::AnimationChannel::Path::SCALE:
					{
						assert(sampler.keyframe_data.size() == sampler.keyframe_times.size() * 3);
						transform.scale_local = ((const XMFLOAT3*)sampler.keyframe_data.data())[keyLeft];
					}
					break;
					}
				}
				else
				{
					// Linear interpolation method:
					float right = sampler.keyframe_times[keyRight];
					float t = (animation.timer - left) / (right - left);

					switch (channel.path)
					{
					case AnimationComponent::AnimationChannel::Path::TRANSLATION:
					{
						assert(sampler.keyframe_data.size() == sampler.keyframe_times.size() * 3);
						const XMFLOAT3* data = (const XMFLOAT3*)sampler.keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat3(&data[keyLeft]);
						XMVECTOR vRight = XMLoadFloat3(&data[keyRight]);
						XMVECTOR vAnim = XMVectorLerp(vLeft, vRight, t);
						XMStoreFloat3(&transform.translation_local, vAnim);
					}
					break;
					case AnimationComponent::AnimationChannel::Path::ROTATION:
					{
						assert(sampler.keyframe_data.size() == sampler.keyframe_times.size() * 4);
						const XMFLOAT4* data = (const XMFLOAT4*)sampler.keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat4(&data[keyLeft]);
						XMVECTOR vRight = XMLoadFloat4(&data[keyRight]);
						XMVECTOR vAnim = XMQuaternionSlerp(vLeft, vRight, t);
						vAnim = XMQuaternionNormalize(vAnim);
						XMStoreFloat4(&transform.rotation_local, vAnim);
					}
					break;
					case AnimationComponent::AnimationChannel::Path::SCALE:
					{
						assert(sampler.keyframe_data.size() == sampler.keyframe_times.size() * 3);
						const XMFLOAT3* data = (const XMFLOAT3*)sampler.keyframe_data.data();
						XMVECTOR vLeft = XMLoadFloat3(&data[keyLeft]);
						XMVECTOR vRight = XMLoadFloat3(&data[keyRight]);
						XMVECTOR vAnim = XMVectorLerp(vLeft, vRight, t);
						XMStoreFloat3(&transform.scale_local, vAnim);
					}
					break;
					}
				}

				transform.SetDirty();

			}

			if (animation.IsPlaying())
			{
				animation.timer += dt;
			}

			const float animationLength = animation.GetLength();

			if (animation.IsLooped() && animation.timer > animationLength)
			{
				animation.timer = 0.0f;
			}

			animation.timer = min(animation.timer, animationLength);
		}
	}
	void RunTransformUpdateSystem(ComponentManager<TransformComponent>& transforms)
	{
		for (size_t i = 0; i < transforms.GetCount(); ++i)
		{
			TransformComponent& transform = transforms[i];
			transform.UpdateTransform();
		}
	}
	void RunHierarchyUpdateSystem(
		const ComponentManager<HierarchyComponent>& hierarchy,
		ComponentManager<TransformComponent>& transforms,
		ComponentManager<LayerComponent>& layers
		)
	{
		for (size_t i = 0; i < hierarchy.GetCount(); ++i)
		{
			const HierarchyComponent& parentcomponent = hierarchy[i];
			Entity entity = hierarchy.GetEntity(i);

			TransformComponent* transform_child = transforms.GetComponent(entity);
			TransformComponent* transform_parent = transforms.GetComponent(parentcomponent.parentID);
			if (transform_child != nullptr && transform_parent != nullptr)
			{
				transform_child->UpdateParentedTransform(*transform_parent, parentcomponent.world_parent_inverse_bind);
			}


			LayerComponent* layer_child = layers.GetComponent(entity);
			LayerComponent* layer_parent = layers.GetComponent(parentcomponent.parentID);
			if (layer_child != nullptr && layer_parent != nullptr)
			{
				layer_child->layerMask = parentcomponent.layerMask_bind & layer_parent->GetLayerMask();
			}

		}
	}
	void RunArmatureUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		ComponentManager<ArmatureComponent>& armatures
	)
	{
		for (size_t i = 0; i < armatures.GetCount(); ++i)
		{
			ArmatureComponent& armature = armatures[i];
			Entity entity = armatures.GetEntity(i);

			if (armature.skinningMatrices.size() != armature.boneCollection.size())
			{
				armature.skinningMatrices.resize(armature.boneCollection.size());
			}

			XMMATRIX R = XMLoadFloat4x4(&armature.remapMatrix);

			int boneIndex = 0;
			for (Entity boneEntity : armature.boneCollection)
			{
				const TransformComponent& bone = *transforms.GetComponent(boneEntity);

				XMMATRIX B = XMLoadFloat4x4(&armature.inverseBindMatrices[boneIndex]);
				XMMATRIX W = XMLoadFloat4x4(&bone.world);
				XMMATRIX M = B * W * R;

				XMStoreFloat4x4(&armature.skinningMatrices[boneIndex++], M);
			}

		}
	}
	void RunMaterialUpdateSystem(ComponentManager<MaterialComponent>& materials, float dt)
	{
		for (size_t i = 0; i < materials.GetCount(); ++i)
		{
			MaterialComponent& material = materials[i];

			material.texAnimSleep -= dt * material.texAnimFrameRate;
			if (material.texAnimSleep <= 0)
			{
				material.texMulAdd.z = fmodf(material.texMulAdd.z + material.texAnimDirection.x, 1);
				material.texMulAdd.w = fmodf(material.texMulAdd.w + material.texAnimDirection.y, 1);
				material.texAnimSleep = 1.0f;

				material.SetDirty(); // will trigger constant buffer update!
			}

			material.engineStencilRef = STENCILREF_DEFAULT;
			if (material.subsurfaceScattering > 0)
			{
				material.engineStencilRef = STENCILREF_SKIN;
			}

		}
	}
	void RunObjectUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		const ComponentManager<MeshComponent>& meshes,
		const ComponentManager<MaterialComponent>& materials,
		ComponentManager<ObjectComponent>& objects,
		ComponentManager<AABB>& aabb_objects,
		AABB& sceneBounds,
		XMFLOAT4& waterPlane
	)
	{
		assert(objects.GetCount() == aabb_objects.GetCount());

		sceneBounds = AABB();

		for (size_t i = 0; i < objects.GetCount(); ++i)
		{
			ObjectComponent& object = objects[i];
			Entity entity = objects.GetEntity(i);
			AABB& aabb = aabb_objects[i];

			aabb = AABB();
			object.rendertypeMask = 0;
			object.SetDynamic(false);
			object.SetCastShadow(false);

			if (object.meshID != INVALID_ENTITY)
			{
				const TransformComponent* transform = transforms.GetComponent(entity);
				const MeshComponent* mesh = meshes.GetComponent(object.meshID);

				if (mesh != nullptr && transform != nullptr)
				{
					aabb = mesh->aabb.get(transform->world);
					sceneBounds = AABB::Merge(sceneBounds, aabb);

					object.position = transform->GetPosition();

					if (mesh->IsSkinned() || mesh->IsDynamic())
					{
						object.SetDynamic(true);
					}

					for (auto& subset : mesh->subsets)
					{
						const MaterialComponent* material = materials.GetComponent(subset.materialID);

						if (material != nullptr)
						{
							if (material->IsTransparent())
							{
								object.rendertypeMask |= RENDERTYPE_TRANSPARENT;
							}
							else
							{
								object.rendertypeMask |= RENDERTYPE_OPAQUE;
							}

							if (material->IsWater())
							{
								object.rendertypeMask |= RENDERTYPE_TRANSPARENT | RENDERTYPE_WATER;

								XMVECTOR _refPlane = XMPlaneFromPointNormal(transform->GetPositionV(), XMVectorSet(0, 1, 0, 0));
								XMStoreFloat4(&waterPlane, _refPlane);
							}

							object.SetCastShadow(material->IsCastingShadow());
						}
					}
				}
			}
		}
	}
	void RunCameraUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		ComponentManager<CameraComponent>& cameras
	)
	{
		for (size_t i = 0; i < cameras.GetCount(); ++i)
		{
			CameraComponent& camera = cameras[i];
			Entity entity = cameras.GetEntity(i);
			const TransformComponent* transform = transforms.GetComponent(entity);
			camera.UpdateCamera(transform);
		}
	}
	void RunDecalUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		const ComponentManager<MaterialComponent>& materials,
		ComponentManager<AABB>& aabb_decals,
		ComponentManager<DecalComponent>& decals
	)
	{
		assert(decals.GetCount() == aabb_decals.GetCount());

		for (size_t i = 0; i < decals.GetCount(); ++i)
		{
			DecalComponent& decal = decals[i];
			Entity entity = decals.GetEntity(i);
			const TransformComponent& transform = *transforms.GetComponent(entity);
			decal.world = transform.world;

			XMMATRIX W = XMLoadFloat4x4(&decal.world);
			XMVECTOR front = XMVectorSet(0, 0, 1, 0);
			front = XMVector3TransformNormal(front, W);
			XMStoreFloat3(&decal.front, front);

			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, W);
			XMStoreFloat3(&decal.position, T);
			XMFLOAT3 scale;
			XMStoreFloat3(&scale, S);
			decal.range = max(scale.x, max(scale.y, scale.z)) * 2;

			AABB& aabb = aabb_decals[i];
			aabb.createFromHalfWidth(XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1));
			aabb = aabb.get(transform.world);

			const MaterialComponent& material = *materials.GetComponent(entity);
			decal.color = material.baseColor;
			decal.emissive = material.emissive;
			decal.texture = material.GetBaseColorMap();
			decal.normal = material.GetNormalMap();
		}
	}
	void RunProbeUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		ComponentManager<AABB>& aabb_probes,
		ComponentManager<EnvironmentProbeComponent>& probes
	)
	{
		assert(probes.GetCount() == aabb_probes.GetCount());

		for (size_t i = 0; i < probes.GetCount(); ++i)
		{
			EnvironmentProbeComponent& probe = probes[i];
			Entity entity = probes.GetEntity(i);
			const TransformComponent& transform = *transforms.GetComponent(entity);

			probe.position = transform.GetPosition();

			XMMATRIX W = XMLoadFloat4x4(&transform.world);
			XMStoreFloat4x4(&probe.inverseMatrix, XMMatrixInverse(nullptr, W));

			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, W);
			XMFLOAT3 scale;
			XMStoreFloat3(&scale, S);
			probe.range = max(scale.x, max(scale.y, scale.z)) * 2;

			AABB& aabb = aabb_probes[i];
			aabb.createFromHalfWidth(XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1));
			aabb = aabb.get(transform.world);
		}
	}
	void RunForceUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		ComponentManager<ForceFieldComponent>& forces
	)
	{
		for (size_t i = 0; i < forces.GetCount(); ++i)
		{
			ForceFieldComponent& force = forces[i];
			Entity entity = forces.GetEntity(i);
			const TransformComponent& transform = *transforms.GetComponent(entity);
			force.position = transform.GetPosition();
			XMStoreFloat3(&force.direction, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, -1, 0, 0), XMLoadFloat4x4(&transform.world))));
		}
	}
	void RunLightUpdateSystem(
		const CameraComponent& cascadeCamera,
		const ComponentManager<TransformComponent>& transforms,
		ComponentManager<AABB>& aabb_lights,
		ComponentManager<LightComponent>& lights,
		WeatherComponent* weather
	)
	{
		assert(lights.GetCount() == aabb_lights.GetCount());

		for (size_t i = 0; i < lights.GetCount(); ++i)
		{
			LightComponent& light = lights[i];
			Entity entity = lights.GetEntity(i);
			const TransformComponent& transform = *transforms.GetComponent(entity);
			AABB& aabb = aabb_lights[i];

			XMMATRIX W = XMLoadFloat4x4(&transform.world);
			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, W);

			XMStoreFloat3(&light.position, T);
			XMStoreFloat4(&light.rotation, R);
			XMStoreFloat3(&light.direction, XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), W));

			switch (light.type)
			{
			case LightComponent::DIRECTIONAL:
			{
				if (weather != nullptr)
				{
					weather->sunColor = light.color;
					weather->sunDirection = light.direction;
				}

				if (light.IsCastingShadow())
				{
					XMFLOAT2 screen = XMFLOAT2((float)wiRenderer::GetInternalResolution().x, (float)wiRenderer::GetInternalResolution().y);
					float nearPlane = cascadeCamera.zNearP;
					float farPlane = cascadeCamera.zFarP;
					XMMATRIX view = cascadeCamera.GetView();
					XMMATRIX projection = cascadeCamera.GetRealProjection();
					XMMATRIX world = XMMatrixIdentity();

					// Set up three shadow cascades (far - mid - near):
					const float referenceFrustumDepth = 800.0f;									// this was the frustum depth used for reference
					const float currentFrustumDepth = farPlane - nearPlane;						// current frustum depth
					const float lerp0 = referenceFrustumDepth / currentFrustumDepth * 0.5f;		// third slice distance from cam (percentage)
					const float lerp1 = referenceFrustumDepth / currentFrustumDepth * 0.12f;	// second slice distance from cam (percentage)
					const float lerp2 = referenceFrustumDepth / currentFrustumDepth * 0.016f;	// first slice distance from cam (percentage)

					// Create shadow cascades for main camera:
					if (light.shadowCam_dirLight.empty())
					{
						light.shadowCam_dirLight.resize(3);
					}

					// Place the shadow cascades inside the viewport:
					if (!light.shadowCam_dirLight.empty())
					{
						// frustum top left - near
						XMVECTOR a0 = XMVector3Unproject(XMVectorSet(0, 0, 0, 1), 0, 0, screen.x, screen.y, 0.0f, 1.0f, projection, view, world);
						// frustum top left - far
						XMVECTOR a1 = XMVector3Unproject(XMVectorSet(0, 0, 1, 1), 0, 0, screen.x, screen.y, 0.0f, 1.0f, projection, view, world);
						// frustum bottom right - near
						XMVECTOR b0 = XMVector3Unproject(XMVectorSet(screen.x, screen.y, 0, 1), 0, 0, screen.x, screen.y, 0.0f, 1.0f, projection, view, world);
						// frustum bottom right - far
						XMVECTOR b1 = XMVector3Unproject(XMVectorSet(screen.x, screen.y, 1, 1), 0, 0, screen.x, screen.y, 0.0f, 1.0f, projection, view, world);

						// calculate cascade projection sizes:
						float size0 = XMVectorGetX(XMVector3Length(XMVectorSubtract(XMVectorLerp(b0, b1, lerp0), XMVectorLerp(a0, a1, lerp0))));
						float size1 = XMVectorGetX(XMVector3Length(XMVectorSubtract(XMVectorLerp(b0, b1, lerp1), XMVectorLerp(a0, a1, lerp1))));
						float size2 = XMVectorGetX(XMVector3Length(XMVectorSubtract(XMVectorLerp(b0, b1, lerp2), XMVectorLerp(a0, a1, lerp2))));

						XMVECTOR rotDefault = XMQuaternionIdentity();

						// create shadow cascade projections:
						light.shadowCam_dirLight[0] = LightComponent::SHCAM(size0, rotDefault, -farPlane * 0.5f, farPlane * 0.5f);
						light.shadowCam_dirLight[1] = LightComponent::SHCAM(size1, rotDefault, -farPlane * 0.5f, farPlane * 0.5f);
						light.shadowCam_dirLight[2] = LightComponent::SHCAM(size2, rotDefault, -farPlane * 0.5f, farPlane * 0.5f);

						// frustum center - near
						XMVECTOR c = XMVector3Unproject(XMVectorSet(screen.x * 0.5f, screen.y * 0.5f, 0, 1), 0, 0, screen.x, screen.y, 0.0f, 1.0f, projection, view, world);
						// frustum center - far
						XMVECTOR d = XMVector3Unproject(XMVectorSet(screen.x * 0.5f, screen.y * 0.5f, 1, 1), 0, 0, screen.x, screen.y, 0.0f, 1.0f, projection, view, world);

						// Avoid shadowmap texel swimming by aligning them to a discrete grid:
						float f0 = light.shadowCam_dirLight[0].size / (float)wiRenderer::GetShadowRes2D();
						float f1 = light.shadowCam_dirLight[1].size / (float)wiRenderer::GetShadowRes2D();
						float f2 = light.shadowCam_dirLight[2].size / (float)wiRenderer::GetShadowRes2D();
						XMVECTOR e0 = XMVectorFloor(XMVectorLerp(c, d, lerp0) / f0) * f0;
						XMVECTOR e1 = XMVectorFloor(XMVectorLerp(c, d, lerp1) / f1) * f1;
						XMVECTOR e2 = XMVectorFloor(XMVectorLerp(c, d, lerp2) / f2) * f2;

						XMMATRIX rrr = XMMatrixRotationQuaternion(R);

						light.shadowCam_dirLight[0].Update(rrr, e0);
						light.shadowCam_dirLight[1].Update(rrr, e1);
						light.shadowCam_dirLight[2].Update(rrr, e2);
					}
				}

				aabb.createFromHalfWidth(wiRenderer::GetCamera().Eye, XMFLOAT3(10000, 10000, 10000));
			}
			break;
			case LightComponent::SPOT:
			{
				if (light.IsCastingShadow())
				{
					if (light.shadowCam_spotLight.empty())
					{
						light.shadowCam_spotLight.push_back(LightComponent::SHCAM(XMFLOAT4(0, 0, 0, 1), 0.1f, 1000.0f, light.fov));
					}
					light.shadowCam_spotLight[0].Update(W);
					light.shadowCam_spotLight[0].farplane = light.range;
					light.shadowCam_spotLight[0].Create_Perspective(light.fov);
				}

				aabb.createFromHalfWidth(light.position, XMFLOAT3(light.range, light.range, light.range));
			}
			break;
			case LightComponent::POINT:
			case LightComponent::SPHERE:
			case LightComponent::DISC:
			case LightComponent::RECTANGLE:
			case LightComponent::TUBE:
			{
				if (light.IsCastingShadow())
				{
					if (light.shadowCam_pointLight.empty())
					{
						light.shadowCam_pointLight.push_back(LightComponent::SHCAM(XMFLOAT4(0.5f, -0.5f, -0.5f, -0.5f), 0.1f, 1000.0f, XM_PIDIV2)); //+x
						light.shadowCam_pointLight.push_back(LightComponent::SHCAM(XMFLOAT4(0.5f, 0.5f, 0.5f, -0.5f), 0.1f, 1000.0f, XM_PIDIV2)); //-x

						light.shadowCam_pointLight.push_back(LightComponent::SHCAM(XMFLOAT4(1, 0, 0, -0), 0.1f, 1000.0f, XM_PIDIV2)); //+y
						light.shadowCam_pointLight.push_back(LightComponent::SHCAM(XMFLOAT4(0, 0, 0, -1), 0.1f, 1000.0f, XM_PIDIV2)); //-y

						light.shadowCam_pointLight.push_back(LightComponent::SHCAM(XMFLOAT4(0.707f, 0, 0, -0.707f), 0.1f, 1000.0f, XM_PIDIV2)); //+z
						light.shadowCam_pointLight.push_back(LightComponent::SHCAM(XMFLOAT4(0, 0.707f, 0.707f, 0), 0.1f, 1000.0f, XM_PIDIV2)); //-z
					}
					for (auto& x : light.shadowCam_pointLight) 
					{
						x.Update(T);
						x.farplane = max(1.0f, light.range);
						x.Create_Perspective(XM_PIDIV2);
					}
				}

				if (light.type == LightComponent::POINT)
				{
					aabb.createFromHalfWidth(light.position, XMFLOAT3(light.range, light.range, light.range));
				}
				else
				{
					XMStoreFloat3(&light.right, XMVector3TransformNormal(XMVectorSet(-1, 0, 0, 0), W));
					XMStoreFloat3(&light.front, XMVector3TransformNormal(XMVectorSet(0, 0, -1, 0), W));
					// area lights have no bounds, just like directional lights (maybe todo)
					aabb.createFromHalfWidth(wiRenderer::GetCamera().Eye, XMFLOAT3(10000, 10000, 10000));
				}
			}
			break;
			}

		}
	}
	void RunParticleUpdateSystem(
		const ComponentManager<TransformComponent>& transforms,
		const ComponentManager<MeshComponent>& meshes,
		ComponentManager<wiEmittedParticle>& emitters,
		ComponentManager<wiHairParticle>& hairs,
		float dt
	)
	{
		for (size_t i = 0; i < emitters.GetCount(); ++i)
		{
			wiEmittedParticle& emitter = emitters[i];
			emitter.Update(dt);
		}

		for (size_t i = 0; i < hairs.GetCount(); ++i)
		{
			wiHairParticle& hair = hairs[i];
			Entity entity = hairs.GetEntity(i);
			const TransformComponent& transform = *transforms.GetComponent(entity);
			hair.world = transform.world;

			if (hair.meshID != INVALID_ENTITY)
			{
				const MeshComponent* mesh = meshes.GetComponent(hair.meshID);

				if (mesh != nullptr)
				{
					XMFLOAT3 min = mesh->aabb.getMin();
					XMFLOAT3 max = mesh->aabb.getMax();

					max.x += hair.length;
					max.y += hair.length;
					max.z += hair.length;

					min.x -= hair.length;
					min.y -= hair.length;
					min.z -= hair.length;

					hair.aabb = AABB(min, max);
					hair.aabb = hair.aabb.get(hair.world);
				}
			}

		}
	}

}