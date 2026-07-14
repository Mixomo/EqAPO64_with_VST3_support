#pragma once

#include "IFilterFactory.h"
#include "ParametricEQFilter.h"

class ParametricEQFilterFactory : public IFilterFactory
{
public:
	std::vector<IFilter*> createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;

	static std::vector<ParametricEQFilter::Band> parseBands(const std::wstring& parameters);
};
