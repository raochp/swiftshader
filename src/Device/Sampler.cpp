// Copyright 2016 The SwiftShader Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Sampler.hpp"

#include "Context.hpp"
#include "Pipeline/PixelRoutine.hpp"
#include "Vulkan/VkDebug.hpp"
#include "Vulkan/VkImage.hpp"

#include <cstring>

namespace sw
{
	FilterType Sampler::maximumTextureFilterQuality = FILTER_LINEAR;
	MipmapType Sampler::maximumMipmapFilterQuality = MIPMAP_POINT;

	Sampler::State::State()
	{
		memset(this, 0, sizeof(State));
	}

	Sampler::Sampler()
	{
		// FIXME: Mipmap::init
		static const unsigned int zero = 0x00FF00FF;

		for(int level = 0; level < MIPMAP_LEVELS; level++)
		{
			Mipmap &mipmap = texture.mipmap[level];

			memset(&mipmap, 0, sizeof(Mipmap));

			for(int face = 0; face < 6; face++)
			{
				mipmap.buffer[face] = &zero;
			}
		}

		textureFormat = VK_FORMAT_UNDEFINED;
		textureType = TEXTURE_NULL;

		textureFilter = FILTER_LINEAR;
		addressingModeU = ADDRESSING_WRAP;
		addressingModeV = ADDRESSING_WRAP;
		addressingModeW = ADDRESSING_WRAP;
		mipmapFilterState = MIPMAP_NONE;
		sRGB = false;
		gather = false;
		highPrecisionFiltering = false;
		border = 0;

		swizzle = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };

		compare = COMPARE_BYPASS;

		exp2LOD = 1.0f;

		texture.maxLod = MAX_TEXTURE_LOD;
		texture.minLod = 0;
	}

	Sampler::~Sampler()
	{
	}

	Sampler::State Sampler::samplerState() const
	{
		State state;

		if(textureType != TEXTURE_NULL)
		{
			state.textureType = textureType;
			state.textureFormat = textureFormat;
			state.textureFilter = getTextureFilter();
			state.addressingModeU = getAddressingModeU();
			state.addressingModeV = getAddressingModeV();
			state.addressingModeW = getAddressingModeW();
			state.mipmapFilter = mipmapFilter();
			state.swizzle = swizzle;
			state.highPrecisionFiltering = highPrecisionFiltering;
			state.compare = getCompareFunc();

			#if PERF_PROFILE
				state.compressedFormat = Surface::isCompressed(externalTextureFormat);
			#endif
		}

		return state;
	}

	void Sampler::setTextureLevel(int face, int level, vk::Image *image, TextureType type)
	{
		if(image)
		{
			Mipmap &mipmap = texture.mipmap[level];

			border = image->isCube() ? 1 : 0;
			VkImageSubresourceLayers subresourceLayers =
			{
				VK_IMAGE_ASPECT_COLOR_BIT,
				static_cast<uint32_t>(level),
				static_cast<uint32_t>(face),
				1
			};
			mipmap.buffer[face] = image->getTexelPointer({ -border, -border, 0 }, subresourceLayers);

			if(face == 0)
			{
				VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT; // FIXME: get proper aspect
				textureFormat = image->getFormat(aspect);

				VkExtent3D mipLevelExtent = image->getMipLevelExtent(level);
				int width = mipLevelExtent.width;
				int height = mipLevelExtent.height;
				int depth = mipLevelExtent.depth;
				int pitchP = image->rowPitchBytes(aspect, level);
				int sliceP = image->slicePitchBytes(aspect, level);

				if(level == 0)
				{
					texture.widthHeightLOD[0] = width * exp2LOD;
					texture.widthHeightLOD[1] = width * exp2LOD;
					texture.widthHeightLOD[2] = height * exp2LOD;
					texture.widthHeightLOD[3] = height * exp2LOD;

					texture.widthLOD[0] = width * exp2LOD;
					texture.widthLOD[1] = width * exp2LOD;
					texture.widthLOD[2] = width * exp2LOD;
					texture.widthLOD[3] = width * exp2LOD;

					texture.heightLOD[0] = height * exp2LOD;
					texture.heightLOD[1] = height * exp2LOD;
					texture.heightLOD[2] = height * exp2LOD;
					texture.heightLOD[3] = height * exp2LOD;

					texture.depthLOD[0] = depth * exp2LOD;
					texture.depthLOD[1] = depth * exp2LOD;
					texture.depthLOD[2] = depth * exp2LOD;
					texture.depthLOD[3] = depth * exp2LOD;
				}

				if(textureFormat.isFloatFormat())
				{
					mipmap.fWidth[0] = (float)width / 65536.0f;
					mipmap.fWidth[1] = (float)width / 65536.0f;
					mipmap.fWidth[2] = (float)width / 65536.0f;
					mipmap.fWidth[3] = (float)width / 65536.0f;

					mipmap.fHeight[0] = (float)height / 65536.0f;
					mipmap.fHeight[1] = (float)height / 65536.0f;
					mipmap.fHeight[2] = (float)height / 65536.0f;
					mipmap.fHeight[3] = (float)height / 65536.0f;

					mipmap.fDepth[0] = (float)depth / 65536.0f;
					mipmap.fDepth[1] = (float)depth / 65536.0f;
					mipmap.fDepth[2] = (float)depth / 65536.0f;
					mipmap.fDepth[3] = (float)depth / 65536.0f;
				}

				short halfTexelU = 0x8000 / width;
				short halfTexelV = 0x8000 / height;
				short halfTexelW = 0x8000 / depth;

				mipmap.uHalf[0] = halfTexelU;
				mipmap.uHalf[1] = halfTexelU;
				mipmap.uHalf[2] = halfTexelU;
				mipmap.uHalf[3] = halfTexelU;

				mipmap.vHalf[0] = halfTexelV;
				mipmap.vHalf[1] = halfTexelV;
				mipmap.vHalf[2] = halfTexelV;
				mipmap.vHalf[3] = halfTexelV;

				mipmap.wHalf[0] = halfTexelW;
				mipmap.wHalf[1] = halfTexelW;
				mipmap.wHalf[2] = halfTexelW;
				mipmap.wHalf[3] = halfTexelW;

				mipmap.width[0] = width;
				mipmap.width[1] = width;
				mipmap.width[2] = width;
				mipmap.width[3] = width;

				mipmap.height[0] = height;
				mipmap.height[1] = height;
				mipmap.height[2] = height;
				mipmap.height[3] = height;

				mipmap.depth[0] = depth;
				mipmap.depth[1] = depth;
				mipmap.depth[2] = depth;
				mipmap.depth[3] = depth;

				mipmap.onePitchP[0] = 1;
				mipmap.onePitchP[1] = pitchP;
				mipmap.onePitchP[2] = 1;
				mipmap.onePitchP[3] = pitchP;

				mipmap.pitchP[0] = pitchP;
				mipmap.pitchP[1] = pitchP;
				mipmap.pitchP[2] = pitchP;
				mipmap.pitchP[3] = pitchP;

				mipmap.sliceP[0] = sliceP;
				mipmap.sliceP[1] = sliceP;
				mipmap.sliceP[2] = sliceP;
				mipmap.sliceP[3] = sliceP;

				if(textureFormat.hasYuvFormat())
				{
					unsigned int YStride = pitchP;
					unsigned int YSize = YStride * height;
					unsigned int CStride = align<16>(YStride / 2);
					unsigned int CSize = CStride * height / 2;

					mipmap.buffer[1] = (byte*)mipmap.buffer[0] + YSize;
					mipmap.buffer[2] = (byte*)mipmap.buffer[1] + CSize;

					texture.mipmap[1].width[0] = width / 2;
					texture.mipmap[1].width[1] = width / 2;
					texture.mipmap[1].width[2] = width / 2;
					texture.mipmap[1].width[3] = width / 2;
					texture.mipmap[1].height[0] = height / 2;
					texture.mipmap[1].height[1] = height / 2;
					texture.mipmap[1].height[2] = height / 2;
					texture.mipmap[1].height[3] = height / 2;
					texture.mipmap[1].onePitchP[0] = 1;
					texture.mipmap[1].onePitchP[1] = CStride;
					texture.mipmap[1].onePitchP[2] = 1;
					texture.mipmap[1].onePitchP[3] = CStride;
				}
			}
		}

		textureType = type;
	}

	void Sampler::setTextureFilter(FilterType textureFilter)
	{
		this->textureFilter = (FilterType)min(textureFilter, maximumTextureFilterQuality);
	}

	void Sampler::setMipmapFilter(MipmapType mipmapFilter)
	{
		mipmapFilterState = (MipmapType)min(mipmapFilter, maximumMipmapFilterQuality);
	}

	void Sampler::setGatherEnable(bool enable)
	{
		gather = enable;
	}

	void Sampler::setAddressingModeU(AddressingMode addressingMode)
	{
		addressingModeU = addressingMode;
	}

	void Sampler::setAddressingModeV(AddressingMode addressingMode)
	{
		addressingModeV = addressingMode;
	}

	void Sampler::setAddressingModeW(AddressingMode addressingMode)
	{
		addressingModeW = addressingMode;
	}

	void Sampler::setMaxAnisotropy(float maxAnisotropy)
	{
		texture.maxAnisotropy = maxAnisotropy;
	}

	void Sampler::setHighPrecisionFiltering(bool highPrecisionFiltering)
	{
		this->highPrecisionFiltering = highPrecisionFiltering;
	}

	void Sampler::setCompareFunc(CompareFunc compare)
	{
		this->compare = compare;
	}

	void Sampler::setMinLod(float minLod)
	{
		texture.minLod = clamp(minLod, 0.0f, (float)(MAX_TEXTURE_LOD));
	}

	void Sampler::setMaxLod(float maxLod)
	{
		texture.maxLod = clamp(maxLod, 0.0f, (float)(MAX_TEXTURE_LOD));
	}

	void Sampler::setFilterQuality(FilterType maximumFilterQuality)
	{
		Sampler::maximumTextureFilterQuality = maximumFilterQuality;
	}

	void Sampler::setMipmapQuality(MipmapType maximumFilterQuality)
	{
		Sampler::maximumMipmapFilterQuality = maximumFilterQuality;
	}

	void Sampler::setMipmapLOD(float LOD)
	{
		exp2LOD = exp2(LOD);
	}

	bool Sampler::hasTexture() const
	{
		return textureType != TEXTURE_NULL;
	}

	bool Sampler::hasUnsignedTexture() const
	{
		return textureFormat.isUnsignedComponent(0) &&
		       textureFormat.isUnsignedComponent(1) &&
		       textureFormat.isUnsignedComponent(2) &&
		       textureFormat.isUnsignedComponent(3);
	}

	bool Sampler::hasCubeTexture() const
	{
		return textureType == TEXTURE_CUBE;
	}

	bool Sampler::hasVolumeTexture() const
	{
		return textureType == TEXTURE_3D || textureType == TEXTURE_2D_ARRAY;
	}

	const Texture &Sampler::getTextureData()
	{
		return texture;
	}

	MipmapType Sampler::mipmapFilter() const
	{
		if(mipmapFilterState != MIPMAP_NONE)
		{
			for(int i = 1; i < MIPMAP_LEVELS; i++)
			{
				if(texture.mipmap[0].buffer[0] != texture.mipmap[i].buffer[0])
				{
					return mipmapFilterState;
				}
			}
		}

		// Only one mipmap level
		return MIPMAP_NONE;
	}

	TextureType Sampler::getTextureType() const
	{
		return textureType;
	}

	FilterType Sampler::getTextureFilter() const
	{
		// Don't filter 1x1 textures.
		if(texture.mipmap[0].width[0] == 1 && texture.mipmap[0].height[0] == 1 && texture.mipmap[0].depth[0] == 1)
		{
			if(mipmapFilter() == MIPMAP_NONE)
			{
				return FILTER_POINT;
			}
		}

		FilterType filter = textureFilter;

		if(gather && textureFormat.componentCount() == 1)
		{
			filter = FILTER_GATHER;
		}

		if(textureType != TEXTURE_2D || texture.maxAnisotropy == 1.0f)
		{
			return (FilterType)min(filter, FILTER_LINEAR);
		}

		return filter;
	}

	AddressingMode Sampler::getAddressingModeU() const
	{
		if(textureType == TEXTURE_CUBE)
		{
			return border ? ADDRESSING_SEAMLESS : ADDRESSING_CLAMP;
		}

		return addressingModeU;
	}

	AddressingMode Sampler::getAddressingModeV() const
	{
		if(textureType == TEXTURE_CUBE)
		{
			return border ? ADDRESSING_SEAMLESS : ADDRESSING_CLAMP;
		}

		return addressingModeV;
	}

	AddressingMode Sampler::getAddressingModeW() const
	{
		if(textureType == TEXTURE_2D_ARRAY ||
		   textureType == TEXTURE_2D ||
		   textureType == TEXTURE_CUBE ||
		   textureType == TEXTURE_RECTANGLE)
		{
			return ADDRESSING_LAYER;
		}

		return addressingModeW;
	}

	CompareFunc Sampler::getCompareFunc() const
	{
		if(getTextureFilter() == FILTER_GATHER)
		{
			return COMPARE_BYPASS;
		}

		return compare;
	}
}
