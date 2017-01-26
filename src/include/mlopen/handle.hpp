#ifndef GUARD_MLOPEN_CONTEXT_HPP_
#define GUARD_MLOPEN_CONTEXT_HPP_

#include <mlopen.h>
#include <mlopen/object.hpp>
#include <mlopen/common.hpp>
#include <mlopen/kernel.hpp>
#include <vector>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mlopen {

struct HandleImpl;

struct Handle : mlopenHandle {
	
	Handle();
	Handle(int numStreams, mlopenAcceleratorQueue_t *streams);
    Handle(Handle&&) noexcept;
	~Handle();

	mlopenAcceleratorQueue_t GetStream() const;

    void EnableProfiling(bool enable=true);
	
	void ResetKernelTime(void);
	void AccumKernelTime(float curr_time);

    float GetKernelTime() const;
#if MLOPEN_BACKEND_OPENCL || MLOPEN_BACKEND_HIPOC
    KernelInvoke GetKernel(
            const std::string& algorithm,
            const std::string& network_config,
            const std::string& program_name,
            const std::string& kernel_name,
            const std::vector<size_t>& vld,
            const std::vector<size_t>& vgd,
            const std::string& params,
            bool is_binary = false,
            const compiled_in_params compiled_ins = compiled_in_params::legacy);

    KernelInvoke GetKernel(
        const std::string& algorithm,
        const std::string& network_config);

    Program LoadProgram(const std::string &program_name, std::string params);
	Program LoadBinaryProgram(const std::string &program_name);

    void Finish() const;
    void Flush() const;
#endif

    std::size_t GetLocalMemorySize();
    std::size_t GetMaxComputeUnits();

    std::string GetDeviceName();

    void Copy(ConstData_t src, Data_t dest, int size);

	ManageDataPtr Create(int sz);
	ManageDataPtr& WriteTo(const void* data, ManageDataPtr& ddata, int sz);
    void ReadTo(void* data, const ManageDataPtr& ddata, int sz);

	template<class T>
	ManageDataPtr Create(int sz)
	{
		return this->Create(sz*sizeof(T));
	}

	template<class Container>
    ManageDataPtr Write(const Container& c)
    {
    	using type = typename Container::value_type;
    	auto buf = this->Create<type>(c.size());
    	return std::move(this->WriteTo(reinterpret_cast<const void*>(c.data()), buf, c.size()*sizeof(type)));
    }

    template<class T>
    std::vector<T> Read(const ManageDataPtr& ddata, int sz)
    {
    	std::vector<T> result(sz);
    	this->ReadTo(result.data(), ddata, sz*sizeof(T));
    	return result;
    }

	std::unique_ptr<HandleImpl> impl;
	
};
} // namespace mlopen
MLOPEN_DEFINE_OBJECT(mlopenHandle, mlopen::Handle);


#endif // GUARD_MLOPEN_CONTEXT_HPP_
