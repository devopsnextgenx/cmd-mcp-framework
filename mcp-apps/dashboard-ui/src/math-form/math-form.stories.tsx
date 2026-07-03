import type { Meta, StoryObj } from '@storybook/react';
import { MathFormWidget } from './math-form';

const meta: Meta<typeof MathFormWidget> = {
  title: 'MCP UI/Math Form',
  component: MathFormWidget,
  parameters: {
    layout: 'centered',
  },
};

export default meta;
type Story = StoryObj<typeof MathFormWidget>;

export const Default: Story = {};
