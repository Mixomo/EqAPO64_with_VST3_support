#pragma once

#include "IFilterFactory.h"

class HeadphoneCalibrationFilterFactory : public IFilterFactory
{
public:
	std::vector<IFilter*> createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;
};
