
Atom::Atom(Atom_type* type__, double* position__, double* vector_field__) : 
    type_(type__), symmetry_class_(NULL), offset_aw_(-1), offset_lo_(-1), offset_wf_(-1), apply_uj_correction_(false), 
    uj_correction_l_(-1)
{
    assert(type__);
        
    for (int i = 0; i < 3; i++)
    {
        position_[i] = position__[i];
        vector_field_[i] = vector_field__[i];
    }
}

void Atom::init(int lmax_pot__, int num_mag_dims__, int offset_aw__, int offset_lo__, int offset_wf__)
{
    assert(lmax_pot__ >= 0);
    assert(offset_aw__ >= 0);
    
    offset_aw_ = offset_aw__;
    offset_lo_ = offset_lo__;
    offset_wf_ = offset_wf__;

    lmax_pot_ = lmax_pot__;
    num_mag_dims_ = num_mag_dims__;

    int lmmax = Utils::lmmax_by_lmax(lmax_pot_);

    h_radial_integrals_.set_dimensions(lmmax, type()->indexr().size(), type()->indexr().size());
    h_radial_integrals_.allocate();
    
    veff_.set_dimensions(lmmax, type()->num_mt_points());
    
    b_radial_integrals_.set_dimensions(lmmax, type()->indexr().size(), type()->indexr().size(), num_mag_dims_);
    b_radial_integrals_.allocate();
    
    for (int j = 0; j < 3; j++) beff_[j].set_dimensions(lmmax, type()->num_mt_points());

    occupation_matrix_.set_dimensions(16, 16, 2, 2);
    occupation_matrix_.allocate();
    
    uj_correction_matrix_.set_dimensions(16, 16, 2, 2);
    uj_correction_matrix_.allocate();
}

void Atom::generate_radial_integrals()
{
    Timer t("sirius::Atom::generate_radial_integrals");
    
    int lmmax = Utils::lmmax_by_lmax(lmax_pot_);
    int nmtp = type()->num_mt_points();

    h_radial_integrals_.zero();
    if (num_mag_dims_) b_radial_integrals_.zero();
    
    // copy spherical integrals
    for (int i2 = 0; i2 < type()->indexr().size(); i2++)
    {
        for (int i1 = 0; i1 < type()->indexr().size(); i1++)
            h_radial_integrals_(0, i1, i2) = symmetry_class()->h_spherical_integral(i1, i2);
    }

    #pragma omp parallel default(shared)
    {
        Spline<double> s(nmtp, type()->radial_grid());
        std::vector<double> v(nmtp);

        #pragma omp for
        for (int lm = 1; lm < lmmax; lm++)
        {
            for (int i2 = 0; i2 < type()->indexr().size(); i2++)
            {
                for (int ir = 0; ir < nmtp; ir++)
                    v[ir] = symmetry_class()->radial_function(ir, i2) * veff_(lm, ir);
                
                for (int i1 = 0; i1 <= i2; i1++)
                {
                    for (int ir = 0; ir < nmtp; ir++) s[ir] = symmetry_class()->radial_function(ir, i1) * v[ir];
                    
                    s.interpolate();
                    h_radial_integrals_(lm, i1, i2) = h_radial_integrals_(lm, i2, i1) = s.integrate(2);
                }
            }
        }
    }

    for (int j = 0; j < num_mag_dims_; j++)
    {
        #pragma omp parallel default(shared)
        {
            Spline<double> s(nmtp, type()->radial_grid());
            std::vector<double> v(nmtp);

            #pragma omp for
            for (int lm = 0; lm < lmmax; lm++)
            {
                for (int i2 = 0; i2 < type()->indexr().size(); i2++)
                {
                    for (int ir = 0; ir < nmtp; ir++)
                        v[ir] = symmetry_class()->radial_function(ir, i2) * beff_[j](lm, ir);
                    
                    for (int i1 = 0; i1 <= i2; i1++)
                    {
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = symmetry_class()->radial_function(ir, i1) * v[ir];
                        
                        s.interpolate();
                        b_radial_integrals_(lm, i1, i2, j) = b_radial_integrals_(lm, i2, i1, j) = s.integrate(2);
                    }
                }
            }
        }
    }
}
